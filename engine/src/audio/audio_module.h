#pragma once

#include <string>

#include <core/common.h>
#include <core/module.h>

#include <assets/asset_data.h>
#include <assets/asset_manager.h>

#include "audio_backend.h"
#include "audio_project_settings.h"
#include "audio_sync.h"

namespace wiwa
{
	class Scene;

	// Owns the audio backend (Null or a vendor's) and gives it a frame. Main thread only; the render and physics threads never touch it.
	// Everything else (components, Lua, the editor) talks to the neutral IAudioBackend through here, never to a vendor.
	class AudioModule final : public IModule
	{
	public:
		AudioModule() = default;
		virtual ~AudioModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "AudioModule"; }
		virtual int get_priority() const override { return 20; } // after the scripts (0) and the animation (10): what they did this frame is heard in it

		// Always valid after init(): the Null backend until another one is selected.
		IAudioBackend& backend() { return *m_Backend; }
		const IAudioBackend& backend() const { return *m_Backend; }
		AudioBackendKind active_kind() const { return m_Backend ? m_Backend->kind() : AudioBackendKind::Null; }

		// Stops and replaces the running backend. A backend that is not built in (no SDK) or fails to start is logged as an error, kept in
		// last_error() for the panel, and the Null backend runs instead; the result is false then. Banks loaded before are not carried over.
		bool select_backend(AudioBackendKind kind, const AudioInitInfo& info);
		// Why the last select_backend fell back; empty when it did not.
		const std::string& last_error() const { return m_LastError; }

		// The project choice: selects the backend and loads the banks. False if the backend fell back to Null (last_error() says why).
		bool apply_settings(AssetManager& assets, const AudioProjectSettings& settings);

		// A world exists only while a scene plays: with auto start (the default, a game and the editor's play mode) the module follows the SceneModule's
		// active scene while the game clock plays, and stops when another scene replaces it (the editor's Stop shows the edit scene again). The
		// world is the emitters and the listener of the scene (AudioSync); a paused game suspends the backend. Without auto start (checks)
		// start_world/stop_world drive it, and the world ignores which scene is active.
		void set_auto_start(bool autoStart) { m_AutoStart = autoStart; }
		// Silences the running world and starts a new one for the scene. True; false only without a backend.
		bool start_world(Ref<Scene> scene);
		// Destroys the emitters and stops all sound. Does nothing when no world runs.
		void stop_world();
		Ref<Scene> world_scene() const { return m_HasWorld ? m_World.lock() : nullptr; }
		EmitterHandle emitter_of(entt::entity entity) const { return m_Sync.emitter_of(entity); }
		// The emitter of an entity of the scene that plays, made on the spot if the next frame has not yet (a script that posts in its first frame).
		// With auto start this also starts the world for the scene, which is only asked for while the game plays. Invalid when the entity has no
		// AudioEmitter or the scene is not the one that plays.
		EmitterHandle emitter_for(const Ref<Scene>& scene, entt::entity entity);
		// One frame for the given active scene and clock state. update() calls it with the engine's; a check can call it without an Engine.
		void tick(const Ref<Scene>& active, bool playing);

		// Plays an event from an emitter of its own at the listener, for the editor to try a sound without the game running. Only while no world plays
		// (a playing scene owns the listener); an invalid id otherwise, or when the backend did not start the event. Stopped by stop_audition and by a
		// world that starts or stops.
		PlayingId audition(std::string_view event);
		void stop_audition();
		bool auditioning() const { return m_AuditionEmitter.valid(); }

		// A soundbank the backend has loaded, with its metadata.
		struct LoadedBank
		{
			std::string Name{};
			AssetRef Ref{};
			AudioBankInfo Info{};
			bool HasInfo = false;
		};

		// Loads a SoundBank asset in the background; update() hands it to the backend, in request order, once it is ready ("Init" always goes first, a
		// Wwise bank cannot load before it). A bank that fails to load or is refused by the backend is logged and dropped.
		void load_bank(AssetManager& assets, const AssetRef& ref);
		// Hands a bank to the backend now. False, and why in error, if the backend refused it.
		bool load_bank_data(const SoundBankData& bank, const AssetRef& ref, std::string& error);
		// Unloads every bank and forgets the ones still loading.
		void unload_banks();
		const std::vector<LoadedBank>& banks() const { return m_Banks; }
		// The order banks are given to the backend: Init first, the others as they were. (poll_pending uses it once every bank has loaded.)
		static void sort_init_first(std::vector<const SoundBankData*>& banks);
		u32 pending_banks() const { return static_cast<u32>(m_Pending.size()); }
		// The metadata of every loaded bank that has it (the event browser and the gizmos read it).
		std::vector<const AudioBankInfo*> bank_infos() const;
		// True if a loaded bank's metadata lists the event.
		bool event_known(std::string_view event) const;

	private:
		struct PendingBank
		{
			AssetRef Ref{};
			Handle<SoundBankData> Asset{};
		};

		void poll_pending();

		Scope<IAudioBackend> m_Backend;
		AudioInitInfo m_Info{};
		std::string m_LastError{};
		std::vector<LoadedBank> m_Banks;
		std::vector<PendingBank> m_Pending;
		AudioSync m_Sync;
		std::weak_ptr<Scene> m_World;
		bool m_HasWorld = false;
		bool m_AutoStart = true;
		bool m_BackendPaused = false;
		EmitterHandle m_AuditionEmitter{};
	};
}
