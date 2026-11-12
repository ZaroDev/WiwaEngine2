#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/asset_data.h>
#include <assets/asset_types.h>
#include <assets/handle.h>
#include <core/module.h>
#include <scene/entity_guid.h>
#include <ui/ui_layout.h>
#include <ui/ui_widget.h>

#include "video_player.h"

namespace wiwa
{
	class Scene;
	class UiModule;

	// A Video widget: the canvas entity that shows the panel, and the widget's id in it.
	struct VideoKey
	{
		EntityGuid Canvas{};
		UiId Widget = 0;

		bool operator==(const VideoKey& other) const { return Canvas == other.Canvas && Widget == other.Widget; }
	};

	struct VideoKeyHash
	{
		size_t operator()(const VideoKey& key) const { return std::hash<EntityGuid>{}(key.Canvas) * 31u + key.Widget; }
	};

	// The bytes of a Video asset once it is Ready (valid until the next call).
	struct VideoAssetBytes
	{
		LoadState State = LoadState::Loading;
		const VideoData* Data = nullptr;
	};

	// Plays the video of a Video widget (UI.find(..):video_play() in Lua, or AutoPlay): one VideoPlayer, the picture sent to the renderer, the sound
	// through miniaudio. Main thread. Priority 13: after the UI's interaction and the scripts (they start and stop videos in their frame), before
	// the UI builds its draw list (it asks view() for the picture).
	//
	// One video plays at a time (a cutscene): playing another stops the first. The video follows its widget: a hidden widget or canvas, or a game
	// clock that stands still, pauses it; another scene, a destroyed canvas or widget stops it. While the game clock is not playing (the editor's
	// edit mode) nothing starts.
	class VideoModule : public IModule
	{
	public:
		using DecoderFactory = std::function<Scope<IVideoDecoder>()>;
		using VideoLoader = std::function<VideoAssetBytes(const AssetRef&)>;
		using FrameSink = std::function<void(u32 stream, std::shared_ptr<const VideoFrame> frame)>;
		using ReleaseSink = std::function<void(u32 stream)>;

		VideoModule() = default;
		virtual ~VideoModule() override;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "VideoModule"; }
		virtual int get_priority() const override { return 13; }

		// One frame, public for tests and tools: the scene that is active, whether the game clock plays, and a monotonic time in microseconds.
		void tick(const Ref<Scene>& active, bool playing, i64 wallUs);

		// Control, from the scripts and the checks. play() false (with last_error()) when the widget does not exist; a widget that has no video asset
		// or whose asset cannot be played is Failed, with the reason in error(), and reports Finished{Failed}.
		// `scene` is the scene the key's canvas is in (scripts pass theirs); without one the scene of the last tick, then the engine's active scene.
		bool play(const VideoKey& key, const Ref<Scene>& scene = nullptr);
		void pause(const VideoKey& key);
		void stop(const VideoKey& key); // Finished{Skipped}
		void seek(const VideoKey& key, double seconds);

		// Idle for a widget that plays nothing.
		VideoState state(const VideoKey& key) const;
		double time(const VideoKey& key) const;
		double duration(const VideoKey& key) const;
		std::string error(const VideoKey& key) const;

		// The picture playing in the widget (the UI asks): its texture id and size, once the first picture has been decoded.
		std::optional<UiVideoView> view(const VideoKey& key) const;

		// The videos that finished since the last call, oldest first; the UI module hands them to the scripts' handlers at the start of the next frame.
		std::vector<std::pair<VideoKey, VideoFinished>> take_finished();

		const std::string& last_error() const { return m_LastError; }
		size_t player_count() const { return m_Entry ? 1 : 0; }

		// Where things come from. The defaults are the platform decoder, the AssetManager, the UiModule and RenderModule of the engine, and the sound
		// card; tests and tools put their own (null for the UI module reads the engine's).
		void set_ui_module(UiModule* ui) { m_Ui = ui; }
		void set_decoder_factory(DecoderFactory factory) { m_Factory = std::move(factory); }
		void set_video_loader(VideoLoader loader) { m_Loader = std::move(loader); }
		void set_frame_sink(FrameSink sink) { m_FrameSink = std::move(sink); }
		void set_release_sink(ReleaseSink sink) { m_ReleaseSink = std::move(sink); }
		void set_audio_mode(VideoAudioMode mode) { m_AudioMode = mode; }

	private:
		struct Entry
		{
			VideoKey Key{};
			std::weak_ptr<Scene> SceneRef{};
			AssetRef Asset{};
			bool Loop = false;
			float Volume = 1.0f;
			Scope<VideoPlayer> Player{};
			VideoState Fallback = VideoState::Loading; // the state while there is no player: Loading, or Failed
			std::string FallbackError{};
			bool PlayWanted = false;
			bool PauseWanted = false;           // a pause and a seek that came before the player existed (the asset was still loading)
			std::optional<double> PendingSeek{};
			bool PausedByUs = false; // the widget is hidden or the game clock stands still
			u32 Stream = 0;
			u64 UploadedSerial = 0;
		};

		Ref<Scene> current_scene() const;
		UiModule* ui() const;
		// The widget of a key in the scene, and whether it can be seen (the canvas, the panel and every parent are visible, none fully transparent).
		const UiWidget* find_widget(Scene& scene, const VideoKey& key, bool* visible) const;
		VideoAssetBytes load(const AssetRef& asset);
		void fail(Entry& entry, const std::string& reason);
		void destroy_entry();
		void service_entry(bool playing, bool visible, bool haveWidget, const UiWidget* widget, i64 wallUs);
		void autoplay(const Ref<Scene>& scene, bool playing);
		void send(u32 stream, std::shared_ptr<const VideoFrame> frame);
		void release(u32 stream);

		UiModule* m_Ui = nullptr;
		DecoderFactory m_Factory{};
		VideoLoader m_Loader{};
		FrameSink m_FrameSink{};
		ReleaseSink m_ReleaseSink{};
		VideoAudioMode m_AudioMode = VideoAudioMode::Device;
		bool m_Initialized = false;

		std::optional<Entry> m_Entry{};
		std::optional<VideoKey> m_EndedKey{}; // the video that played out and whose player was let go: its state is still Ended
		std::vector<std::pair<VideoKey, VideoFinished>> m_Finished{};
		std::string m_LastError{};
		std::weak_ptr<Scene> m_Scene{};
		std::unordered_map<VideoKey, bool, VideoKeyHash> m_WasVisible{}; // AutoPlay starts a widget when it becomes visible
		std::unordered_map<AssetGuid, Handle<VideoData>> m_Handles{};
		u32 m_NextStream = 1;
		i64 m_WallUs = 0;
	};
}
