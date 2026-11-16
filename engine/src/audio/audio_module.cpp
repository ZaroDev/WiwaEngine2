#include <pch.h>
#include "audio_module.h"

#include <algorithm>

#include <core/engine.h>
#include <scene/scene.h>
#include <scene/scene_module.h>

#include "mini/mini_backend.h"
#include "null_backend.h"

#ifdef WI_AUDIO_WWISE
#include "wwise/wwise_backend.h"
#endif

#define WI_AUDIO_LOG(level, ...) WI_LOG(level, "Audio", __VA_ARGS__)

namespace wiwa
{
	void AudioModule::init()
	{
		select_backend(AudioBackendKind::Null, m_Info);

		// A game: the audio settings the editor packed with it (the editor has none in its Library and applies audio.wsettings itself).
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets && assets->get_source())
		{
			if (const std::optional<AudioProjectSettings> settings = read_audio_settings(*assets->get_source()))
			{
				apply_settings(*assets, *settings);
			}
		}
	}

	bool AudioModule::apply_settings(AssetManager& assets, const AudioProjectSettings& settings)
	{
		AudioInitInfo info;
		info.MemoryPoolKb = settings.MemoryPoolKb;
		info.Language = settings.Language;
		const bool started = select_backend(settings.Backend, info);
		for (const AssetRef& bank : settings.Banks)
		{
			load_bank(assets, bank);
		}
		return started;
	}

	void AudioModule::shutdown()
	{
		stop_world();
		stop_audition();
		if (m_Backend)
		{
			unload_banks();
			m_Backend->stop_all();
			m_Backend->shutdown();
			m_Backend.reset();
		}
	}

	void AudioModule::update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		tick(scenes != nullptr ? scenes->get_active() : nullptr, Engine::get().get_time().get_game_clock().IsPlaying);
	}

	void AudioModule::tick(const Ref<Scene>& active, bool playing)
	{
		poll_pending();
		if (!m_Backend)
		{
			return;
		}

		// the scene was replaced or is gone: its sound goes with it. A start waits for the banks to load: an event posted on start would find no bank
		// and never play.
		m_Follow.follow(active, playing, [this] { stop_world(); }, [this](const Ref<Scene>& scene) { start_world(scene); }, m_Pending.empty());

		if (m_Follow.has_world())
		{
			if (m_BackendPaused == playing)
			{
				m_BackendPaused = !playing;
				m_Backend->set_paused(m_BackendPaused);
			}
			if (playing)
			{
				if (const Ref<Scene> scene = m_Follow.scene())
				{
					m_Sync.update(*m_Backend, *scene);
				}
			}
		}
		m_Backend->update();
	}

	bool AudioModule::start_world(Ref<Scene> scene)
	{
		stop_world();
		stop_audition(); // a playing scene owns the listener
		if (!m_Backend || !scene)
		{
			return false;
		}
		m_Follow.bind(scene);
		return true;
	}

	PlayingId AudioModule::audition(std::string_view event)
	{
		if (!m_Backend || m_Follow.has_world() || event.empty())
		{
			return {};
		}
		if (!m_AuditionEmitter.valid())
		{
			m_AuditionEmitter = m_Backend->create_emitter("Audition");
			m_Backend->set_listener(AudioTransform{});
			m_Backend->set_emitter(m_AuditionEmitter, AudioTransform{}); // at the listener: heard at full volume
		}
		return m_Backend->post_event(event, m_AuditionEmitter);
	}

	void AudioModule::stop_audition()
	{
		if (m_Backend && m_AuditionEmitter.valid())
		{
			m_Backend->destroy_emitter(m_AuditionEmitter); // stops what it plays
		}
		m_AuditionEmitter = {};
	}

	EmitterHandle AudioModule::emitter_for(const Ref<Scene>& scene, entt::entity entity)
	{
		if (!m_Backend || !scene)
		{
			return {};
		}
		if (!m_Follow.has_world() && m_Follow.auto_start())
		{
			start_world(scene);
		}
		if (!m_Follow.has_world() || m_Follow.scene() != scene)
		{
			return {};
		}
		return m_Sync.ensure(*m_Backend, *scene, entity);
	}

	void AudioModule::stop_world()
	{
		if (!m_Follow.has_world())
		{
			return;
		}
		if (m_Backend)
		{
			m_Sync.stop(*m_Backend);
			if (m_BackendPaused)
			{
				m_Backend->set_paused(false);
			}
		}
		m_BackendPaused = false;
		m_Follow.release();
	}

	bool AudioModule::select_backend(AudioBackendKind kind, const AudioInitInfo& info)
	{
		stop_world();
		stop_audition();
		if (m_Backend)
		{
			unload_banks();
			m_Backend->stop_all();
			m_Backend->shutdown();
			m_Backend.reset();
		}

		m_Info = info;
		m_LastError.clear();

		Scope<IAudioBackend> backend;
		std::string error;
		switch (kind)
		{
		case AudioBackendKind::Null:
			backend = CreateScope<NullAudioBackend>();
			break;
		case AudioBackendKind::MiniAudio:
			backend = CreateScope<MiniAudioBackend>(Engine::get().get_module<AssetManager>());
			break;
		case AudioBackendKind::Wwise:
#ifdef WI_AUDIO_WWISE
			backend = CreateScope<WwiseAudioBackend>();
#else
			error = "The Wwise backend is not built in (set WWISESDK and regenerate the projects)";
#endif
			break;
		}

		if (backend && !backend->init(info, error))
		{
			backend.reset();
		}

		if (!backend)
		{
			m_LastError = error.empty() ? "The audio backend could not start" : error;
			WI_AUDIO_LOG(Error, "Audio backend {0} unavailable, using Null: {1}", audio_backend_name(kind), m_LastError);
			backend = CreateScope<NullAudioBackend>();
			std::string ignored;
			backend->init(info, ignored);
			m_Backend = std::move(backend);
			return false;
		}

		m_Backend = std::move(backend);
		return true;
	}

	void AudioModule::load_bank(AssetManager& assets, const AssetRef& ref)
	{
		if (!ref.valid())
		{
			return;
		}
		for (const PendingBank& pending : m_Pending)
		{
			if (pending.Ref.resolved() == ref.resolved())
			{
				return;
			}
		}
		for (const LoadedBank& bank : m_Banks)
		{
			if (bank.Ref.resolved() == ref.resolved())
			{
				return;
			}
		}
		m_Pending.push_back({ ref, assets.load<SoundBankData>(ref) });
	}

	void AudioModule::sort_init_first(std::vector<const SoundBankData*>& banks)
	{
		std::stable_partition(banks.begin(), banks.end(), [](const SoundBankData* bank) { return bank->Name == "Init"; });
	}

	void AudioModule::poll_pending()
	{
		// Nothing goes to the backend until every requested bank has finished loading: which one is Init is only known from its data, and a
		// Wwise bank cannot load before it. Then Init goes first and the others keep their order.
		if (m_Pending.empty())
		{
			return;
		}
		for (const PendingBank& pending : m_Pending)
		{
			if (pending.Asset.state() == LoadState::Loading)
			{
				return;
			}
		}

		std::vector<const SoundBankData*> ready;
		std::vector<AssetRef> refs;
		for (const PendingBank& pending : m_Pending)
		{
			if (pending.Asset.state() == LoadState::Ready && pending.Asset.get())
			{
				ready.push_back(pending.Asset.get());
				refs.push_back(pending.Ref);
			}
			else
			{
				WI_AUDIO_LOG(Error, "Audio bank {0} could not be loaded", pending.Ref.resolved().to_string());
			}
		}

		std::vector<const SoundBankData*> order = ready;
		sort_init_first(order);
		for (const SoundBankData* data : order)
		{
			const size_t index = static_cast<size_t>(std::find(ready.begin(), ready.end(), data) - ready.begin());
			std::string error;
			if (!load_bank_data(*data, refs[index], error))
			{
				WI_AUDIO_LOG(Error, "Audio bank '{0}' was not loaded: {1}", data->Name, error);
			}
		}
		m_Pending.clear(); // the handles go after the backend took the bytes (it copies them)
	}

	bool AudioModule::load_bank_data(const SoundBankData& bank, const AssetRef& ref, std::string& error)
	{
		if (!m_Backend)
		{
			error = "no audio backend";
			return false;
		}
		if (!m_Backend->load_bank(bank.Name, bank.Bytes, error))
		{
			return false;
		}
		if (bank.HasInfo)
		{
			std::vector<std::string> names;
			names.reserve(bank.Info.Events.size());
			for (const AudioEventInfo& event : bank.Info.Events)
			{
				names.push_back(event.Name);
			}
			m_Backend->note_bank_events(bank.Name, names);
		}
		m_Banks.push_back({ bank.Name, ref, bank.Info, bank.HasInfo });
		return true;
	}

	void AudioModule::unload_banks()
	{
		if (m_Backend)
		{
			for (const LoadedBank& bank : m_Banks)
			{
				m_Backend->unload_bank(bank.Name);
			}
		}
		m_Banks.clear();
		m_Pending.clear();
	}

	std::vector<const AudioBankInfo*> AudioModule::bank_infos() const
	{
		std::vector<const AudioBankInfo*> infos;
		for (const LoadedBank& bank : m_Banks)
		{
			if (bank.HasInfo)
			{
				infos.push_back(&bank.Info);
			}
		}
		return infos;
	}

	bool AudioModule::event_known(std::string_view event) const
	{
		for (const LoadedBank& bank : m_Banks)
		{
			if (bank.HasInfo && bank.Info.find_event(event))
			{
				return true;
			}
		}
		return false;
	}
}
