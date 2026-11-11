#include <pch.h>

#ifdef WI_AUDIO_WWISE

#include "wwise_backend.h"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_set>

#include <malloc.h>
#include <windows.h>

#include <AK/SoundEngine/Common/AkMemoryMgr.h>
#include <AK/SoundEngine/Common/AkMemoryMgrModule.h>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkStreamMgrModule.h>
#include <AK/SoundEngine/Common/AkCallback.h>
#include <AK/SoundEngine/Common/IAkStreamMgr.h>
#include <AK/SpatialAudio/Common/AkSpatialAudio.h>
#include <AK/Plugin/AkVorbisDecoderFactory.h> // registers the Vorbis codec (banks use it by default)

#define WI_AUDIO_LOG(level, ...) WI_LOG(level, "Audio", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Game objects: the vendor needs one to post on, and one for the listener. Emitters come after.
		constexpr AkGameObjectID GLOBAL_OBJECT = 1;
		constexpr AkGameObjectID LISTENER_OBJECT = 2;
		constexpr AkGameObjectID FIRST_EMITTER_OBJECT = 100;

		AkGameObjectID object_of(EmitterHandle emitter)
		{
			return emitter.valid() ? FIRST_EMITTER_OBJECT + emitter.Value : GLOBAL_OBJECT;
		}

		std::wstring widen(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}

		std::string describe(AKRESULT result)
		{
			switch (result)
			{
			case AK_Success: return "success";
			case AK_Fail: return "failed";
			case AK_InsufficientMemory: return "not enough memory (raise the memory pool in Settings, Audio)";
			case AK_InvalidParameter: return "invalid parameter";
			case AK_InvalidFile: return "invalid or incompatible file (generate the banks with the same Wwise version as the SDK)";
			case AK_IDNotFound: return "not found";
			default: return "error " + std::to_string(static_cast<int>(result));
			}
		}

		// The engine is right-handed (forward -Z) and Wwise left-handed (front +Z): flipping Z of the positions and directions keeps right on the right.
		AkVector to_vector(const AudioVec3& v)
		{
			AkVector vector;
			vector.X = v.X;
			vector.Y = v.Y;
			vector.Z = -v.Z;
			return vector;
		}

		AkVector64 to_vector64(const AudioVec3& v)
		{
			AkVector64 vector;
			vector.X = v.X;
			vector.Y = v.Y;
			vector.Z = -v.Z;
			return vector;
		}

		// The sound engine is process-global: one backend at a time.
		std::atomic<bool> s_InUse{ false };

		AkSoundPosition to_position(const AudioTransform& t)
		{
			AkSoundPosition position;
			position.Set(to_vector64(t.Position), to_vector(t.Forward), to_vector(t.Up));
			return position;
		}
	}

	struct WwiseState
	{
		struct Bank
		{
			AkBankID Id = 0;
			void* Memory = nullptr; // aligned, kept until the bank is unloaded
		};

		bool Initialized = false;
		bool OwnsEngine = false; // holds the process-wide claim on the sound engine
		bool Suspended = false;
		std::map<std::string, Bank> Banks;
		std::map<u32, bool> Emitters;
		u32 NextEmitter = 1;

		// The playing ids: the end-of-event callback comes from the audio thread.
		mutable std::mutex PlayingMutex;
		std::unordered_set<AkPlayingID> Playing;
		std::unordered_set<std::string> WarnedEvents;

		// Wwise 2025.1 callback: the event info and the cookie come as arguments of their own.
		static void on_event_end(AkCallbackType type, AkEventCallbackInfo* event, void* /*callbackInfo*/, void* cookie)
		{
			if (type == AK_EndOfEvent && event != nullptr && cookie != nullptr)
			{
				WwiseState* self = static_cast<WwiseState*>(cookie);
				std::lock_guard<std::mutex> lock(self->PlayingMutex);
				self->Playing.erase(event->playingID);
			}
		}
	};

	WwiseAudioBackend::WwiseAudioBackend() : m_State(std::make_unique<WwiseState>()) {}

	WwiseAudioBackend::~WwiseAudioBackend()
	{
		shutdown();
	}

	bool WwiseAudioBackend::init(const AudioInitInfo& info, std::string& error)
	{
		if (s_InUse.exchange(true))
		{
			error = "Wwise: another Wwise backend is already running (the sound engine is process-global)";
			return false;
		}
		struct Claim
		{
			bool Keep = false;
			~Claim() { if (!Keep) { s_InUse = false; } }
		} claim; // released when init fails; kept (and released in shutdown) when it succeeds
		// The order the SDK documents: memory, streaming, sound engine, music engine, spatial audio. Banks load from memory, so no streaming
		// device is made: the banks must be generated with their media inside (not streamed).
		AkMemSettings memorySettings;
		AK::MemoryMgr::GetDefaultSettings(memorySettings);
		// Wwise 2025.1 manages its memory in arenas of its own: the pool size of the settings is not applied (info.MemoryPoolKb is kept for other backends).
		if (AK::MemoryMgr::Init(&memorySettings) != AK_Success)
		{
			error = "Wwise: the memory manager did not start";
			return false;
		}

		AkStreamMgrSettings streamSettings;
		AK::StreamMgr::GetDefaultSettings(streamSettings);
		if (AK::StreamMgr::Create(streamSettings) == nullptr)
		{
			AK::MemoryMgr::Term();
			error = "Wwise: the stream manager did not start";
			return false;
		}
		const std::wstring language = widen(info.Language);
		AK::StreamMgr::SetCurrentLanguage(language.c_str());

		AkInitSettings initSettings;
		AkPlatformInitSettings platformSettings;
		AK::SoundEngine::GetDefaultInitSettings(initSettings);
		AK::SoundEngine::GetDefaultPlatformInitSettings(platformSettings);
		AKRESULT result = AK::SoundEngine::Init(&initSettings, &platformSettings);
		if (result != AK_Success)
		{
			AK::IAkStreamMgr::Get()->Destroy();
			AK::MemoryMgr::Term();
			error = "Wwise: the sound engine did not start: " + describe(result);
			return false;
		}

		AkSpatialAudioInitSettings spatialSettings;
		AK::SpatialAudio::Init(spatialSettings);

		AK::SoundEngine::RegisterGameObj(GLOBAL_OBJECT, "Global");
		AK::SoundEngine::RegisterGameObj(LISTENER_OBJECT, "Listener");
		const AkGameObjectID listener = LISTENER_OBJECT;
		AK::SoundEngine::SetDefaultListeners(&listener, 1);

		m_State->Initialized = true;
		m_State->OwnsEngine = true;
		claim.Keep = true;
		return true;
	}

	void WwiseAudioBackend::shutdown()
	{
		if (!m_State || !m_State->Initialized)
		{
			return;
		}

		// Stop the sound, unload the banks, then take the engine down in reverse order.
		AK::SoundEngine::StopAll();
		AK::SoundEngine::RenderAudio();
		for (auto& [name, bank] : m_State->Banks)
		{
			AK::SoundEngine::UnloadBank(bank.Id, bank.Memory);
			_aligned_free(bank.Memory);
		}
		m_State->Banks.clear();
		m_State->Emitters.clear();
		{
			std::lock_guard<std::mutex> lock(m_State->PlayingMutex);
			m_State->Playing.clear();
		}

		AK::SoundEngine::Term();
		if (AK::IAkStreamMgr::Get() != nullptr)
		{
			AK::IAkStreamMgr::Get()->Destroy();
		}
		AK::MemoryMgr::Term();
		m_State->Initialized = false;
		if (m_State->OwnsEngine)
		{
			m_State->OwnsEngine = false;
			s_InUse = false;
		}
	}

	void WwiseAudioBackend::update()
	{
		if (m_State->Initialized)
		{
			AK::SoundEngine::RenderAudio();
		}
	}

	void WwiseAudioBackend::set_listener(const AudioTransform& transform)
	{
		AK::SoundEngine::SetPosition(LISTENER_OBJECT, to_position(transform));
	}

	EmitterHandle WwiseAudioBackend::create_emitter(std::string_view debugName)
	{
		const EmitterHandle handle{ m_State->NextEmitter++ };
		const std::string name(debugName);
		AK::SoundEngine::RegisterGameObj(object_of(handle), name.c_str());
		m_State->Emitters[handle.Value] = true;
		return handle;
	}

	void WwiseAudioBackend::destroy_emitter(EmitterHandle emitter)
	{
		if (m_State->Emitters.erase(emitter.Value) != 0)
		{
			AK::SoundEngine::StopAll(object_of(emitter));
			AK::SoundEngine::UnregisterGameObj(object_of(emitter));
		}
	}

	void WwiseAudioBackend::set_emitter(EmitterHandle emitter, const AudioTransform& transform)
	{
		AK::SoundEngine::SetPosition(object_of(emitter), to_position(transform));
	}

	void WwiseAudioBackend::set_emitter_scale(EmitterHandle emitter, float scale)
	{
		AK::SoundEngine::SetScalingFactor(object_of(emitter), scale);
	}

	void WwiseAudioBackend::set_paused(bool paused)
	{
		if (paused && !m_State->Suspended)
		{
			AK::SoundEngine::Suspend();
			m_State->Suspended = true;
		}
		else if (!paused && m_State->Suspended)
		{
			AK::SoundEngine::WakeupFromSuspend();
			m_State->Suspended = false;
		}
	}

	PlayingId WwiseAudioBackend::post_event(std::string_view event, EmitterHandle emitter)
	{
		const std::string name(event);
		const AkPlayingID id = AK::SoundEngine::PostEvent(name.c_str(), object_of(emitter), AK_EndOfEvent, &WwiseState::on_event_end, m_State.get());
		if (id == AK_INVALID_PLAYING_ID)
		{
			if (m_State->WarnedEvents.insert(name).second)
			{
				WI_AUDIO_LOG(Warning, "Wwise could not start the event '{0}' (not in a loaded bank?)", name);
			}
			return {};
		}
		{
			std::lock_guard<std::mutex> lock(m_State->PlayingMutex);
			m_State->Playing.insert(id);
		}
		return PlayingId{ id };
	}

	void WwiseAudioBackend::stop_playing(PlayingId playing)
	{
		if (playing.valid())
		{
			AK::SoundEngine::StopPlayingID(static_cast<AkPlayingID>(playing.Value));
		}
	}

	void WwiseAudioBackend::stop_emitter(EmitterHandle emitter)
	{
		AK::SoundEngine::StopAll(object_of(emitter));
	}

	void WwiseAudioBackend::stop_all()
	{
		if (m_State->Initialized)
		{
			AK::SoundEngine::StopAll();
		}
	}

	void WwiseAudioBackend::set_rtpc(std::string_view name, float value, EmitterHandle emitter)
	{
		const std::string key(name);
		const AKRESULT result = emitter.valid() ? AK::SoundEngine::SetRTPCValue(key.c_str(), value, object_of(emitter)) : AK::SoundEngine::SetRTPCValue(key.c_str(), value);
		if (result != AK_Success && m_State->WarnedEvents.insert("rtpc:" + key).second)
		{
			WI_AUDIO_LOG(Warning, "Wwise could not set the RTPC '{0}': {1}", key, describe(result));
		}
	}

	void WwiseAudioBackend::set_switch(std::string_view group, std::string_view state, EmitterHandle emitter)
	{
		const std::string groupName(group);
		const std::string stateName(state);
		const AKRESULT result = AK::SoundEngine::SetSwitch(groupName.c_str(), stateName.c_str(), object_of(emitter));
		if (result != AK_Success && m_State->WarnedEvents.insert("switch:" + groupName + "/" + stateName).second)
		{
			WI_AUDIO_LOG(Warning, "Wwise could not set the switch '{0}/{1}': {2}", groupName, stateName, describe(result));
		}
	}

	void WwiseAudioBackend::set_state(std::string_view group, std::string_view state)
	{
		const std::string groupName(group);
		const std::string stateName(state);
		const AKRESULT result = AK::SoundEngine::SetState(groupName.c_str(), stateName.c_str());
		if (result != AK_Success && m_State->WarnedEvents.insert("state:" + groupName + "/" + stateName).second)
		{
			WI_AUDIO_LOG(Warning, "Wwise could not set the state '{0}/{1}': {2}", groupName, stateName, describe(result));
		}
	}

	bool WwiseAudioBackend::load_bank(std::string_view name, std::span<const std::byte> data, std::string& error)
	{
		const std::string key(name);
		if (m_State->Banks.contains(key))
		{
			error = "the bank is already loaded";
			return false;
		}

		// The bank is read from memory the sound engine keeps using until it is unloaded; Wwise wants it aligned.
		void* memory = _aligned_malloc(data.size(), AK_BANK_PLATFORM_DATA_ALIGNMENT);
		if (memory == nullptr)
		{
			error = "not enough memory for the bank";
			return false;
		}
		std::memcpy(memory, data.data(), data.size());

		AkBankID id = 0;
		const AKRESULT result = AK::SoundEngine::LoadBankMemoryView(memory, static_cast<AkUInt32>(data.size()), id);
		if (result != AK_Success)
		{
			_aligned_free(memory);
			error = describe(result);
			return false;
		}
		m_State->Banks[key] = { id, memory };
		return true;
	}

	void WwiseAudioBackend::unload_bank(std::string_view name)
	{
		const auto found = m_State->Banks.find(std::string(name));
		if (found == m_State->Banks.end())
		{
			return;
		}
		AK::SoundEngine::UnloadBank(found->second.Id, found->second.Memory);
		_aligned_free(found->second.Memory);
		m_State->Banks.erase(found);
	}

	std::vector<std::string> WwiseAudioBackend::loaded_banks() const
	{
		std::vector<std::string> names;
		for (const auto& [name, bank] : m_State->Banks)
		{
			names.push_back(name);
		}
		return names;
	}

	u32 WwiseAudioBackend::playing_count() const
	{
		std::lock_guard<std::mutex> lock(m_State->PlayingMutex);
		return static_cast<u32>(m_State->Playing.size());
	}
}

#endif
