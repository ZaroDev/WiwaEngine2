#pragma once

#include <memory>

#include <audio/audio_backend.h>

// A small audio backend on the miniaudio library that needs no vendor SDK: an event is an AudioClip asset (a wav, mp3 or flac) with the event's name,
// played with position, volume and looping. Clips come through the AssetManager, so the same files are used from the editor's Library and from a game's
// pak. No miniaudio type is visible here: they live behind MiniState in mini_backend.cpp.
namespace wiwa
{
	class AssetManager;
	struct MiniState;

	class MiniAudioBackend final : public IAudioBackend
	{
	public:
		// `assets` finds and loads the clips (without it every event is unknown). `noDevice` makes no sound card: the engine only renders when
		// render() is called (for checks on a machine without audio).
		explicit MiniAudioBackend(AssetManager* assets, bool noDevice = false);
		virtual ~MiniAudioBackend() override;

		virtual AudioBackendKind kind() const override { return AudioBackendKind::MiniAudio; }
		virtual bool init(const AudioInitInfo& info, std::string& error) override;
		virtual void shutdown() override;
		virtual void update() override;

		virtual void set_listener(const AudioTransform& transform) override;
		virtual EmitterHandle create_emitter(std::string_view debugName) override;
		virtual void destroy_emitter(EmitterHandle emitter) override;
		virtual void set_emitter(EmitterHandle emitter, const AudioTransform& transform) override;
		virtual void set_emitter_scale(EmitterHandle emitter, float scale) override;
		virtual void set_paused(bool paused) override;

		// An event is the file name of an AudioClip without its extension, in any letter case; a name ending in _loop loops. A clip that has not
		// finished loading starts a few frames later; the id is valid at once.
		virtual PlayingId post_event(std::string_view event, EmitterHandle emitter) override;
		// Options: loop (default: the name ends in _loop) and volume, which multiplies the emitter's.
		virtual PlayingId post_event_with(std::string_view event, EmitterHandle emitter, const PostOptions& options) override;
		virtual void set_playing_volume(PlayingId playing, float volume) override;
		virtual void stop_playing(PlayingId playing) override;
		virtual void stop_emitter(EmitterHandle emitter) override;
		virtual void stop_all() override;

		// "Volume" (0 to 1 and above, linear) and "Pitch" (1 = as recorded) are the parameters; any other name is ignored. An invalid emitter is global.
		virtual void set_rtpc(std::string_view name, float value, EmitterHandle emitter) override;
		virtual void set_switch(std::string_view, std::string_view, EmitterHandle) override {}
		virtual void set_state(std::string_view, std::string_view) override {}

		// This backend has no banks: the call succeeds and does nothing.
		virtual bool load_bank(std::string_view, std::span<const std::byte>, std::string&) override { return true; }
		virtual void unload_bank(std::string_view) override {}
		virtual std::vector<std::string> loaded_banks() const override { return {}; }

		virtual u32 playing_count() const override;

		// With noDevice: renders this many frames, which is what lets a sound play to its end in a check.
		void render(u32 frames);

	private:
		std::unique_ptr<MiniState> m_State;
	};
}
