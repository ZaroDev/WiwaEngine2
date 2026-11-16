#include <pch.h>
#include "video_module.h"

#include <chrono>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/view/view_types.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <scene/scene_module.h>
#include <ui/ui_module.h>

#include "video_factory.h"

#define WI_VIDEO_LOG(level, ...) WI_LOG(level, "Video", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		i64 wall_clock_us()
		{
			return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// A widget can be seen when it and every widget above it is visible and not fully transparent. (The UI draws nothing for the others.)
		bool widget_shown(const UiPanelData& panel, const UiWidget& widget)
		{
			const UiWidget* current = &widget;
			for (size_t depth = 0; current != nullptr && depth <= panel.Widgets.size(); depth++)
			{
				if (!current->Visible || !(current->Opacity > 0.0f))
				{
					return false;
				}
				current = current->Parent != 0 ? panel.find(current->Parent) : nullptr;
			}
			return true;
		}
	}

	VideoModule::~VideoModule()
	{
		destroy_entry();
	}

	void VideoModule::init()
	{
		if (!m_Factory)
		{
			m_Factory = []() { return create_platform_decoder(); };
		}
		if (UiModule* ui = this->ui())
		{
			ui->set_video_provider([this](const EntityGuid& canvas, UiId widget) { return view(VideoKey{ canvas, widget }); });
			ui->set_video_event_source([this]()
				{
					std::vector<UiModule::UiVideoEvent> events;
					for (const auto& [key, how] : take_finished())
					{
						events.push_back(UiModule::UiVideoEvent{ key.Canvas, key.Widget, how.Skipped, how.Failed });
					}
					return events;
				});
		}
	}

	void VideoModule::shutdown()
	{
		if (UiModule* ui = this->ui())
		{
			ui->set_video_provider({});
			ui->set_video_event_source({});
		}
		destroy_entry();
		m_Handles.clear();
	}

	UiModule* VideoModule::ui() const
	{
		if (m_Ui != nullptr)
		{
			return m_Ui;
		}
		return Engine::get().get_module<UiModule>();
	}

	Ref<Scene> VideoModule::current_scene() const
	{
		if (Ref<Scene> scene = m_Scene.lock())
		{
			return scene;
		}
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		return scenes != nullptr ? scenes->get_active() : nullptr;
	}

	void VideoModule::update(float)
	{
		// Without a playing game clock and without a video there is nothing to do: the scene is not touched (an edit mode, or a check that loads
		// scenes from another thread, sees no extra access from this module).
		const bool playing = Engine::get().get_time().get_game_clock().IsPlaying;
		if (!playing && !m_Entry)
		{
			return;
		}
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		tick(scenes != nullptr ? scenes->get_active() : nullptr, playing, wall_clock_us());
	}

	const UiWidget* VideoModule::find_widget(Scene& scene, const VideoKey& key, bool* visible) const
	{
		UiModule* ui = this->ui();
		if (visible != nullptr)
		{
			*visible = false;
		}
		if (ui == nullptr)
		{
			return nullptr;
		}
		UiPanelInstance* instance = ui->instance_for(scene, key.Canvas);
		if (instance == nullptr || !instance->ready())
		{
			return nullptr;
		}
		const UiWidget* widget = instance->data().find(key.Widget);
		if (widget == nullptr || widget->Kind != WidgetKind::Video)
		{
			return nullptr;
		}
		if (visible != nullptr)
		{
			const entt::entity canvas = scene.find(key.Canvas);
			const UICanvas* component = canvas != entt::null ? scene.registry().try_get<UICanvas>(canvas) : nullptr;
			*visible = component != nullptr && component->Visible && instance->visible() && widget_shown(instance->data(), *widget);
		}
		return widget;
	}

	VideoAssetBytes VideoModule::load(const AssetRef& asset)
	{
		if (m_Loader)
		{
			return m_Loader(asset);
		}
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets == nullptr || !asset.valid())
		{
			return { LoadState::Failed, nullptr };
		}
		auto it = m_Handles.find(asset.Guid);
		if (it == m_Handles.end())
		{
			it = m_Handles.emplace(asset.Guid, assets->load<VideoData>(asset)).first;
		}
		const LoadState state = it->second.state();
		return { state, state == LoadState::Ready ? it->second.get() : nullptr };
	}

	void VideoModule::send(u32 stream, std::shared_ptr<const VideoFrame> frame)
	{
		if (m_FrameSink)
		{
			m_FrameSink(stream, std::move(frame));
		}
		else if (RenderModule* render = Engine::get().get_module<RenderModule>())
		{
			render->upload_video_frame(stream, std::move(frame));
		}
	}

	void VideoModule::release(u32 stream)
	{
		if (m_ReleaseSink)
		{
			m_ReleaseSink(stream);
		}
		else if (RenderModule* render = Engine::get().get_module<RenderModule>())
		{
			render->release_video_stream(stream);
		}
	}

	void VideoModule::fail(Entry& entry, const std::string& reason)
	{
		entry.Fallback = VideoState::Failed;
		entry.FallbackError = reason;
		entry.PlayWanted = false;
		m_LastError = reason;
		m_Finished.push_back({ entry.Key, VideoFinished{ false, true } });
		WI_VIDEO_LOG(Error, "Video cannot play: {0}", reason);
	}

	void VideoModule::destroy_entry()
	{
		if (!m_Entry)
		{
			return;
		}
		Entry entry = std::move(*m_Entry);
		m_Entry.reset();
		entry.Player.reset(); // joins the decode worker and the sound
		if (entry.Stream != 0)
		{
			release(entry.Stream);
		}
		if (entry.Asset.valid())
		{
			m_Handles.erase(entry.Asset.Guid);
		}
	}

	bool VideoModule::play(const VideoKey& key, const Ref<Scene>& sceneOfCaller)
	{
		Ref<Scene> scene = sceneOfCaller ? sceneOfCaller : current_scene();
		if (!scene)
		{
			m_LastError = "there is no scene to play a video in";
			return false;
		}
		bool visible = false;
		const UiWidget* widget = find_widget(*scene, key, &visible);
		if (widget == nullptr)
		{
			m_LastError = "there is no Video widget with that id on that canvas";
			return false;
		}

		m_EndedKey.reset();
		if (m_Entry && !(m_Entry->Key == key))
		{
			WI_VIDEO_LOG(Warning, "A second video was started: only one plays at a time, the first one is stopped");
			m_Finished.push_back({ m_Entry->Key, VideoFinished{ true, false } }); // whoever waited for the first one hears that it was cut short
			destroy_entry();
		}
		if (m_Entry && (!(m_Entry->Asset == widget->Video) || m_Entry->Loop != widget->Loop))
		{
			destroy_entry(); // the script changed what the widget plays: this play is of the new video
		}
		if (m_Entry)
		{
			// the same widget: a play goes on from a pause, or starts again from the end
			if (m_Entry->Player)
			{
				m_Entry->Player->play();
			}
			else if (m_Entry->Fallback == VideoState::Loading)
			{
				m_Entry->PlayWanted = true;
				m_Entry->PauseWanted = false;
			}
			return true;
		}

		Entry entry;
		entry.Key = key;
		entry.SceneRef = scene;
		entry.Asset = widget->Video;
		entry.Loop = widget->Loop;
		entry.Volume = widget->Volume;
		entry.PlayWanted = true;
		entry.Stream = m_NextStream++;
		m_Entry = std::move(entry);
		if (!m_Entry->Asset.valid())
		{
			fail(*m_Entry, "the Video widget has no video asset");
		}
		return true;
	}

	void VideoModule::pause(const VideoKey& key)
	{
		if (m_Entry && m_Entry->Key == key)
		{
			if (m_Entry->Player)
			{
				m_Entry->Player->pause();
			}
			else
			{
				m_Entry->PauseWanted = true; // applied when the player exists, after the play that is waiting
			}
			m_Entry->PausedByUs = false; // a pause the script asked for is its own: the clock and the widget do not undo it
		}
	}

	void VideoModule::stop(const VideoKey& key)
	{
		if (!m_Entry || !(m_Entry->Key == key))
		{
			return;
		}
		if (m_Entry->Player)
		{
			m_Entry->Player->stop();
			m_Entry->PausedByUs = false;
		}
		else
		{
			// still loading or already failed: nothing to show
			const bool wasWanted = m_Entry->PlayWanted;
			const VideoKey stopped = m_Entry->Key;
			destroy_entry();
			if (wasWanted)
			{
				m_Finished.push_back({ stopped, VideoFinished{ true, false } });
			}
		}
	}

	void VideoModule::seek(const VideoKey& key, double seconds)
	{
		if (m_Entry && m_Entry->Key == key)
		{
			if (m_Entry->Player)
			{
				m_Entry->Player->seek(seconds);
			}
			else
			{
				m_Entry->PendingSeek = seconds;
			}
		}
	}

	VideoState VideoModule::state(const VideoKey& key) const
	{
		if (!m_Entry || !(m_Entry->Key == key))
		{
			return m_EndedKey && *m_EndedKey == key ? VideoState::Ended : VideoState::Idle;
		}
		return m_Entry->Player ? m_Entry->Player->state() : m_Entry->Fallback;
	}

	double VideoModule::time(const VideoKey& key) const
	{
		return m_Entry && m_Entry->Key == key && m_Entry->Player ? m_Entry->Player->time() : 0.0;
	}

	double VideoModule::duration(const VideoKey& key) const
	{
		return m_Entry && m_Entry->Key == key && m_Entry->Player ? m_Entry->Player->duration() : 0.0;
	}

	std::string VideoModule::error(const VideoKey& key) const
	{
		if (!m_Entry || !(m_Entry->Key == key))
		{
			return {};
		}
		return m_Entry->Player ? m_Entry->Player->error() : m_Entry->FallbackError;
	}

	std::optional<UiVideoView> VideoModule::view(const VideoKey& key) const
	{
		if (!m_Entry || !(m_Entry->Key == key) || !m_Entry->Player)
		{
			return std::nullopt;
		}
		const std::shared_ptr<const VideoFrame> frame = m_Entry->Player->current_frame();
		if (!frame || frame->Width == 0 || frame->Height == 0)
		{
			return std::nullopt;
		}
		return UiVideoView{ view_video_texture_id(m_Entry->Stream), glm::vec2(static_cast<float>(frame->Width), static_cast<float>(frame->Height)) };
	}

	std::vector<std::pair<VideoKey, VideoFinished>> VideoModule::take_finished()
	{
		std::vector<std::pair<VideoKey, VideoFinished>> result;
		result.swap(m_Finished);
		return result;
	}

	void VideoModule::service_entry(bool playing, bool visible, bool haveWidget, const UiWidget* widget, i64 wallUs)
	{
		Entry& entry = *m_Entry;

		// the asset arrives, and the player starts
		if (!entry.Player && entry.Fallback == VideoState::Loading)
		{
			const VideoAssetBytes bytes = load(entry.Asset);
			if (bytes.State == LoadState::Failed)
			{
				fail(entry, "the video asset could not be loaded");
			}
			else if (bytes.State == LoadState::Ready && bytes.Data != nullptr)
			{
				Scope<IVideoDecoder> decoder = m_Factory ? m_Factory() : create_platform_decoder();
				if (!decoder)
				{
					fail(entry, "this platform has no video decoder");
				}
				else
				{
					VideoPlayerDesc desc;
					desc.Loop = entry.Loop;
					desc.Volume = entry.Volume;
					desc.AudioMode = m_AudioMode;
					entry.Player = CreateScope<VideoPlayer>(std::move(decoder), desc);
					entry.Player->open(bytes.Data->Bytes);
					if (entry.PlayWanted)
					{
						entry.Player->play();
					}
					if (entry.PendingSeek)
					{
						entry.Player->seek(*entry.PendingSeek);
					}
					if (entry.PauseWanted)
					{
						entry.Player->pause();
					}
				}
			}
		}
		if (!entry.Player)
		{
			return;
		}

		if (haveWidget && widget != nullptr)
		{
			entry.Player->set_volume(widget->Volume);
		}

		// What the script asked for this frame (play, pause, stop, seek) is applied first, so the state below is the real one
		entry.Player->update(wallUs);

		// the widget is hidden, or the game clock stands still: pause; a pause of ours is undone when both are over, one the script asked for is not
		const VideoState state = entry.Player->state();
		const bool shouldRun = playing && visible;
		bool changed = false;
		if (!shouldRun && state == VideoState::Playing)
		{
			entry.Player->pause();
			entry.PausedByUs = true;
			changed = true;
		}
		else if (shouldRun && entry.PausedByUs)
		{
			if (state == VideoState::Paused)
			{
				entry.Player->play();
				changed = true;
			}
			entry.PausedByUs = false;
		}
		if (changed)
		{
			entry.Player->update(wallUs); // applied now, not a frame later: the picture is paused (or going) in this frame
		}

		while (const std::optional<VideoFinished> finished = entry.Player->take_finished())
		{
			m_Finished.push_back({ entry.Key, *finished });
		}
		if (entry.Player->state() == VideoState::Failed && m_LastError != entry.Player->error())
		{
			m_LastError = entry.Player->error();
		}

		if (entry.Player->frame_serial() != entry.UploadedSerial)
		{
			entry.UploadedSerial = entry.Player->frame_serial();
			if (std::shared_ptr<const VideoFrame> frame = entry.Player->current_frame())
			{
				send(entry.Stream, std::move(frame));
			}
		}

		// A video that is over (or was stopped) and is not shown any more has nothing left to do: its decoder, sound device, picture buffers and
		// texture (and the copies of the file) are let go. Playing it again makes a new player.
		const VideoState now = entry.Player->state();
		if (!visible && (now == VideoState::Ended || now == VideoState::Idle))
		{
			if (now == VideoState::Ended)
			{
				m_EndedKey = entry.Key;
			}
			destroy_entry();
		}
	}

	void VideoModule::autoplay(const Ref<Scene>& scene, bool playing)
	{
		// Nothing starts while the game clock stands still (the editor's edit mode, and the headless checks that load scenes from another thread):
		// the scene is not even looked at then.
		UiModule* ui = this->ui();
		if (!playing || ui == nullptr)
		{
			return;
		}

		for (const auto [entity, id, canvas] : scene->registry().view<Id, UICanvas>().each())
		{
			UiPanelInstance* instance = ui->instance_for(*scene, id.Guid);
			if (instance == nullptr || !instance->ready())
			{
				continue;
			}
			const bool canvasVisible = canvas.Visible && instance->visible();
			for (const UiWidget& widget : instance->data().Widgets)
			{
				if (widget.Kind != WidgetKind::Video)
				{
					continue;
				}
				const VideoKey key{ id.Guid, widget.Id };
				const bool visible = canvasVisible && widget_shown(instance->data(), widget);
				const bool was = m_WasVisible[key];
				m_WasVisible[key] = visible;
				if (playing && widget.AutoPlay && visible && !was && widget.Video.valid())
				{
					const VideoState current = state(key);
					if (current == VideoState::Idle || current == VideoState::Ended)
					{
						play(key);
					}
				}
			}
		}
	}

	void VideoModule::tick(const Ref<Scene>& active, bool playing, i64 wallUs)
	{
		// another scene: the widgets of AutoPlay are new to it
		if (m_Scene.lock() != active)
		{
			m_EndedKey.reset();
			m_WasVisible.clear();
			m_Scene = active;
		}
		// what plays belongs to the scene it was started in (a script can start a video before the first tick has seen the scene)
		if (m_Entry)
		{
			const Ref<Scene> scene = m_Entry->SceneRef.lock();
			if (!scene || scene != active)
			{
				destroy_entry();
			}
		}

		bool visible = false;
		const UiWidget* widget = nullptr;
		if (m_Entry && active)
		{
			widget = find_widget(*active, m_Entry->Key, &visible);
			if (widget == nullptr)
			{
				destroy_entry(); // the canvas or the widget is gone
			}
		}

		if (active)
		{
			autoplay(active, playing);
			if (m_Entry && widget == nullptr)
			{
				widget = find_widget(*active, m_Entry->Key, &visible); // one that autoplay just started
			}
		}

		if (m_Entry)
		{
			service_entry(playing, visible, widget != nullptr, widget, wallUs);
		}
	}
}
