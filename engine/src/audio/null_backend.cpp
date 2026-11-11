#include <pch.h>
#include "null_backend.h"

#include <algorithm>

#define WI_AUDIO_LOG(level, ...) WI_LOG(level, "Audio", __VA_ARGS__)

namespace wiwa
{
	bool NullAudioBackend::init(const AudioInitInfo&, std::string&)
	{
		record("init");
		return true;
	}

	void NullAudioBackend::shutdown()
	{
		record("shutdown");
		m_Playing.clear();
		m_Emitters.clear();
		m_Banks.clear();
	}

	void NullAudioBackend::set_listener(const AudioTransform& transform)
	{
		m_Listener = transform;
		record("listener");
	}

	EmitterHandle NullAudioBackend::create_emitter(std::string_view debugName)
	{
		const EmitterHandle handle{ m_NextEmitter++ };
		m_Emitters.insert(handle.Value);
		record(FMT("create_emitter:{0}={1}", debugName, handle.Value));
		return handle;
	}

	void NullAudioBackend::destroy_emitter(EmitterHandle emitter)
	{
		stop_emitter(emitter);
		m_Emitters.erase(emitter.Value);
		m_EmitterTransforms.erase(emitter.Value);
		record(FMT("destroy_emitter:{0}", emitter.Value));
	}

	void NullAudioBackend::set_emitter(EmitterHandle emitter, const AudioTransform& transform)
	{
		m_EmitterTransforms[emitter.Value] = transform;
		record(FMT("set_emitter:{0}", emitter.Value));
	}

	void NullAudioBackend::set_emitter_scale(EmitterHandle emitter, float scale)
	{
		record(FMT("scale:{0}={1}", emitter.Value, scale));
	}

	void NullAudioBackend::set_paused(bool paused)
	{
		m_Paused = paused;
		record(paused ? "pause" : "resume");
	}

	PlayingId NullAudioBackend::post_event(std::string_view event, EmitterHandle emitter)
	{
		if (m_Strict && !m_KnownEvents.contains(std::string(event)))
		{
			if (m_WarnedEvents.insert(std::string(event)).second)
			{
				m_UnknownWarnings++;
				WI_AUDIO_LOG(Warning, "Unknown audio event '{0}'", event);
			}
			return {};
		}

		const PlayingId id{ m_NextPlaying++ };
		m_Playing.push_back({ id.Value, emitter.Value });
		record(FMT("post:{0}@{1}", event, emitter.Value));
		return id;
	}

	void NullAudioBackend::stop_playing(PlayingId playing)
	{
		std::erase_if(m_Playing, [&](const Playing& p) { return p.Id == playing.Value; });
		record(FMT("stop:{0}", playing.Value));
	}

	void NullAudioBackend::stop_emitter(EmitterHandle emitter)
	{
		std::erase_if(m_Playing, [&](const Playing& p) { return p.Emitter == emitter.Value; });
		record(FMT("stop_emitter:{0}", emitter.Value));
	}

	void NullAudioBackend::stop_all()
	{
		m_Playing.clear();
		record("stop_all");
	}

	void NullAudioBackend::set_rtpc(std::string_view name, float value, EmitterHandle emitter)
	{
		record(FMT("rtpc:{0}={1}@{2}", name, value, emitter.Value));
	}

	void NullAudioBackend::set_switch(std::string_view group, std::string_view state, EmitterHandle emitter)
	{
		record(FMT("switch:{0}={1}@{2}", group, state, emitter.Value));
	}

	void NullAudioBackend::set_state(std::string_view group, std::string_view state)
	{
		record(FMT("state:{0}={1}", group, state));
	}

	bool NullAudioBackend::load_bank(std::string_view name, std::span<const std::byte>, std::string&)
	{
		m_Banks.insert(std::string(name));
		record(FMT("load_bank:{0}", name));
		return true;
	}

	void NullAudioBackend::unload_bank(std::string_view name)
	{
		m_Banks.erase(std::string(name));
		record(FMT("unload_bank:{0}", name));
	}

	std::vector<std::string> NullAudioBackend::loaded_banks() const
	{
		return std::vector<std::string>(m_Banks.begin(), m_Banks.end());
	}

	void NullAudioBackend::note_bank_events(std::string_view, std::span<const std::string> events)
	{
		for (const std::string& event : events)
		{
			m_KnownEvents.insert(event);
		}
	}
}
