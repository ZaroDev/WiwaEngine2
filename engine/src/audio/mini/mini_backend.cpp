#include <pch.h>
#include "mini_backend.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include <miniaudio.h>

#include <assets/asset_manager.h>

#define WI_AUDIO_LOG(level, ...) WI_LOG(level, "Audio", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		std::string lower(std::string_view text)
		{
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		constexpr float DEFAULT_MAX_DISTANCE = 50.0f; // world units an emitter is heard from at scale 1: the clips carry no attenuation of their own
	}

	struct MiniState
	{
		struct Emitter
		{
			AudioTransform Transform{};
			float Scale = 1.0f;
			float Volume = 1.0f;
			float Pitch = 1.0f;
		};

		// One playing (or waiting to play) sound. The decoder reads the clip's encoded bytes as the sound plays (streaming from memory); the
		// Handle keeps those bytes alive.
		struct Instance
		{
			u64 Id = 0;
			u32 Emitter = 0; // 0: global
			std::string Event{};
			Handle<AudioClipData> Clip{};
			bool Loop = false;
			bool Started = false;
			std::unique_ptr<ma_decoder> Decoder{};
			std::unique_ptr<ma_sound> Sound{};
		};

		AssetManager* Assets = nullptr;
		bool NoDevice = false;
		bool EngineReady = false;
		ma_engine Engine{};

		std::map<u32, Emitter> Emitters;
		std::vector<std::unique_ptr<Instance>> Instances;
		u32 NextEmitter = 1;
		u64 NextPlaying = 1;
		float GlobalPitch = 1.0f;
		bool Paused = false;

		std::unordered_map<std::string, AssetRef> Index; // lower-case clip name -> asset
		size_t IndexedEntries = 0;
		std::chrono::steady_clock::time_point LastRebuild{};
		std::unordered_set<std::string> Warned;

		void rebuild_index()
		{
			Index.clear();
			LastRebuild = std::chrono::steady_clock::now();
			if (Assets == nullptr)
			{
				return;
			}
			const std::vector<AssetCatalog::Entry> entries = Assets->catalog_entries();
			IndexedEntries = entries.size();
			for (const AssetCatalog::Entry& entry : entries)
			{
				const std::filesystem::path path(entry.Path);
				const std::string extension = lower(path.extension().string());
				if (extension == ".wav" || extension == ".mp3" || extension == ".flac" || extension == ".ogg")
				{
					AssetRef ref;
					ref.Guid = entry.Guid;
					Index.emplace(lower(path.stem().string()), ref);
				}
			}
		}

		std::optional<AssetRef> find_clip(std::string_view event)
		{
			const std::string key = lower(event);
			auto found = Index.find(key);
			if (found == Index.end() && std::chrono::steady_clock::now() - LastRebuild > std::chrono::seconds(1))
			{
				rebuild_index(); // a clip may have been imported since
				found = Index.find(key);
			}
			return found != Index.end() ? std::optional<AssetRef>(found->second) : std::nullopt;
		}

		Instance* find(u64 id)
		{
			for (auto& instance : Instances)
			{
				if (instance->Id == id)
				{
					return instance.get();
				}
			}
			return nullptr;
		}

		// What an instance takes from its emitter: where it is, how far it is heard from, how loud and at which pitch.
		void apply(Instance& instance)
		{
			if (!instance.Started)
			{
				return;
			}
			ma_sound& sound = *instance.Sound;
			const auto emitter = Emitters.find(instance.Emitter);
			if (instance.Emitter == 0 || emitter == Emitters.end())
			{
				ma_sound_set_spatialization_enabled(&sound, MA_FALSE);
				ma_sound_set_pitch(&sound, GlobalPitch);
				return;
			}

			const Emitter& e = emitter->second;
			ma_sound_set_spatialization_enabled(&sound, MA_TRUE);
			ma_sound_set_attenuation_model(&sound, ma_attenuation_model_linear);
			ma_sound_set_min_distance(&sound, 1.0f);
			ma_sound_set_max_distance(&sound, std::max(2.0f, DEFAULT_MAX_DISTANCE * e.Scale));
			ma_sound_set_position(&sound, e.Transform.Position.X, e.Transform.Position.Y, e.Transform.Position.Z);
			ma_sound_set_direction(&sound, e.Transform.Forward.X, e.Transform.Forward.Y, e.Transform.Forward.Z);
			ma_sound_set_volume(&sound, e.Volume);
			ma_sound_set_pitch(&sound, e.Pitch * GlobalPitch);
		}

		void release(Instance& instance)
		{
			if (instance.Sound)
			{
				if (instance.Started)
				{
					ma_sound_uninit(instance.Sound.get());
				}
				instance.Sound.reset();
			}
			if (instance.Decoder)
			{
				if (instance.Started)
				{
					ma_decoder_uninit(instance.Decoder.get());
				}
				instance.Decoder.reset();
			}
			instance.Started = false;
		}

		// False if the clip cannot be played (the instance is then dropped by the caller).
		bool start(Instance& instance)
		{
			const AudioClipData* clip = instance.Clip.get();
			if (clip == nullptr)
			{
				return false;
			}

			const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, ma_engine_get_channels(&Engine), ma_engine_get_sample_rate(&Engine));
			instance.Decoder = std::make_unique<ma_decoder>();
			if (ma_decoder_init_memory(clip->Bytes.data(), clip->Bytes.size(), &config, instance.Decoder.get()) != MA_SUCCESS)
			{
				instance.Decoder.reset();
				WI_AUDIO_LOG(Warning, "Audio clip '{0}' could not be decoded", instance.Event);
				return false;
			}
			instance.Sound = std::make_unique<ma_sound>();
			if (ma_sound_init_from_data_source(&Engine, instance.Decoder.get(), 0, nullptr, instance.Sound.get()) != MA_SUCCESS)
			{
				ma_decoder_uninit(instance.Decoder.get());
				instance.Decoder.reset();
				instance.Sound.reset();
				WI_AUDIO_LOG(Warning, "Audio clip '{0}' could not be played", instance.Event);
				return false;
			}

			instance.Started = true;
			ma_sound_set_looping(instance.Sound.get(), instance.Loop ? MA_TRUE : MA_FALSE);
			apply(instance);
			ma_sound_start(instance.Sound.get());
			return true;
		}

		void remove_if_finished()
		{
			for (auto it = Instances.begin(); it != Instances.end();)
			{
				Instance& instance = **it;
				const LoadState state = instance.Clip.state();
				bool drop = false;
				if (!instance.Started)
				{
					if (state == LoadState::Failed)
					{
						WI_AUDIO_LOG(Warning, "Audio clip '{0}' could not be loaded", instance.Event);
						drop = true;
					}
					else if (state == LoadState::Ready)
					{
						drop = !start(instance);
					}
				}
				else
				{
					drop = ma_sound_at_end(instance.Sound.get()) == MA_TRUE;
				}

				if (drop)
				{
					release(instance);
					it = Instances.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void stop_where(const std::function<bool(const Instance&)>& match)
		{
			for (auto it = Instances.begin(); it != Instances.end();)
			{
				if (match(**it))
				{
					release(**it);
					it = Instances.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	};

	MiniAudioBackend::MiniAudioBackend(AssetManager* assets, bool noDevice) : m_State(std::make_unique<MiniState>())
	{
		m_State->Assets = assets;
		m_State->NoDevice = noDevice;
	}

	MiniAudioBackend::~MiniAudioBackend()
	{
		shutdown();
	}

	bool MiniAudioBackend::init(const AudioInitInfo&, std::string& error)
	{
		ma_engine_config config = ma_engine_config_init();
		if (m_State->NoDevice)
		{
			config.noDevice = MA_TRUE;
			config.channels = 2;
			config.sampleRate = 48000;
		}
		const ma_result result = ma_engine_init(&config, &m_State->Engine);
		if (result != MA_SUCCESS)
		{
			error = std::string("MiniAudio: the audio engine did not start: ") + ma_result_description(result);
			return false;
		}
		m_State->EngineReady = true;
		m_State->rebuild_index();
		return true;
	}

	void MiniAudioBackend::shutdown()
	{
		if (!m_State->EngineReady)
		{
			return;
		}
		m_State->stop_where([](const MiniState::Instance&) { return true; });
		m_State->Emitters.clear();
		ma_engine_uninit(&m_State->Engine);
		m_State->EngineReady = false;
	}

	void MiniAudioBackend::update()
	{
		if (m_State->EngineReady)
		{
			m_State->remove_if_finished();
		}
	}

	void MiniAudioBackend::render(u32 frames)
	{
		if (!m_State->EngineReady || !m_State->NoDevice)
		{
			return;
		}
		std::vector<float> buffer(static_cast<size_t>(frames) * ma_engine_get_channels(&m_State->Engine));
		ma_uint64 read = 0;
		ma_engine_read_pcm_frames(&m_State->Engine, buffer.data(), frames, &read);
	}

	void MiniAudioBackend::set_listener(const AudioTransform& t)
	{
		// miniaudio is right-handed with -Z forward, as the engine is: the values go through as they are.
		ma_engine_listener_set_position(&m_State->Engine, 0, t.Position.X, t.Position.Y, t.Position.Z);
		ma_engine_listener_set_direction(&m_State->Engine, 0, t.Forward.X, t.Forward.Y, t.Forward.Z);
		ma_engine_listener_set_world_up(&m_State->Engine, 0, t.Up.X, t.Up.Y, t.Up.Z);
	}

	EmitterHandle MiniAudioBackend::create_emitter(std::string_view)
	{
		const EmitterHandle handle{ m_State->NextEmitter++ };
		m_State->Emitters[handle.Value] = {};
		return handle;
	}

	void MiniAudioBackend::destroy_emitter(EmitterHandle emitter)
	{
		stop_emitter(emitter);
		m_State->Emitters.erase(emitter.Value);
	}

	void MiniAudioBackend::set_emitter(EmitterHandle emitter, const AudioTransform& transform)
	{
		const auto found = m_State->Emitters.find(emitter.Value);
		if (found == m_State->Emitters.end())
		{
			return;
		}
		found->second.Transform = transform;
		for (auto& instance : m_State->Instances)
		{
			if (instance->Emitter == emitter.Value)
			{
				m_State->apply(*instance);
			}
		}
	}

	void MiniAudioBackend::set_emitter_scale(EmitterHandle emitter, float scale)
	{
		const auto found = m_State->Emitters.find(emitter.Value);
		if (found == m_State->Emitters.end())
		{
			return;
		}
		found->second.Scale = scale;
		for (auto& instance : m_State->Instances)
		{
			if (instance->Emitter == emitter.Value)
			{
				m_State->apply(*instance);
			}
		}
	}

	void MiniAudioBackend::set_paused(bool paused)
	{
		if (!m_State->EngineReady || m_State->NoDevice || paused == m_State->Paused)
		{
			return;
		}
		m_State->Paused = paused;
		if (paused)
		{
			ma_engine_stop(&m_State->Engine);
		}
		else
		{
			ma_engine_start(&m_State->Engine);
		}
	}

	PlayingId MiniAudioBackend::post_event(std::string_view event, EmitterHandle emitter)
	{
		if (!m_State->EngineReady || m_State->Assets == nullptr)
		{
			return {};
		}
		const std::optional<AssetRef> clip = m_State->find_clip(event);
		if (!clip)
		{
			if (m_State->Warned.insert(std::string(event)).second)
			{
				WI_AUDIO_LOG(Warning, "No audio clip is named '{0}' (a .wav, .mp3 or .flac file with that name in the assets)", event);
			}
			return {};
		}

		auto instance = std::make_unique<MiniState::Instance>();
		instance->Id = m_State->NextPlaying++;
		instance->Emitter = emitter.valid() && m_State->Emitters.contains(emitter.Value) ? emitter.Value : 0;
		instance->Event = std::string(event);
		const std::string key = lower(event);
		instance->Loop = key.size() >= 5 && key.compare(key.size() - 5, 5, "_loop") == 0;
		instance->Clip = m_State->Assets->load<AudioClipData>(*clip);

		const PlayingId id{ instance->Id };
		MiniState::Instance& added = *instance;
		m_State->Instances.push_back(std::move(instance));
		if (added.Clip.ready() && !m_State->start(added))
		{
			m_State->stop_where([&](const MiniState::Instance& other) { return other.Id == id.Value; });
			return {};
		}
		return id;
	}

	void MiniAudioBackend::stop_playing(PlayingId playing)
	{
		m_State->stop_where([&](const MiniState::Instance& instance) { return instance.Id == playing.Value; });
	}

	void MiniAudioBackend::stop_emitter(EmitterHandle emitter)
	{
		m_State->stop_where([&](const MiniState::Instance& instance) { return instance.Emitter == emitter.Value; });
	}

	void MiniAudioBackend::stop_all()
	{
		m_State->stop_where([](const MiniState::Instance&) { return true; });
	}

	void MiniAudioBackend::set_rtpc(std::string_view name, float value, EmitterHandle emitter)
	{
		const std::string key = lower(name);
		const bool global = !emitter.valid();
		const auto found = m_State->Emitters.find(emitter.Value);
		if (key == "volume")
		{
			if (global)
			{
				ma_engine_set_volume(&m_State->Engine, std::max(0.0f, value));
			}
			else if (found != m_State->Emitters.end())
			{
				found->second.Volume = std::max(0.0f, value);
			}
		}
		else if (key == "pitch")
		{
			const float pitch = std::clamp(value, 0.1f, 4.0f);
			if (global)
			{
				m_State->GlobalPitch = pitch;
			}
			else if (found != m_State->Emitters.end())
			{
				found->second.Pitch = pitch;
			}
		}
		else
		{
			return; // not a parameter of this backend
		}

		for (auto& instance : m_State->Instances)
		{
			if (global || instance->Emitter == emitter.Value)
			{
				m_State->apply(*instance);
			}
		}
	}

	u32 MiniAudioBackend::playing_count() const
	{
		return static_cast<u32>(m_State->Instances.size());
	}
}
