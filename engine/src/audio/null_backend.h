#pragma once

#include <map>
#include <set>
#include <unordered_set>

#include "audio_backend.h"

namespace wiwa
{
	// Plays nothing. It is the fallback when the chosen vendor is missing or fails, and what the smoke checks run on: every call is recorded
	// as a line in calls() ("post:Play_Test@1", "rtpc:Volume=0.5@0", "stop_all", ...).
	class NullAudioBackend final : public IAudioBackend
	{
	public:
		virtual AudioBackendKind kind() const override { return AudioBackendKind::Null; }
		virtual bool init(const AudioInitInfo& info, std::string& error) override;
		virtual void shutdown() override;
		virtual void update() override { m_Updates++; }

		virtual void set_listener(const AudioTransform& transform) override;
		virtual EmitterHandle create_emitter(std::string_view debugName) override;
		virtual void destroy_emitter(EmitterHandle emitter) override;
		virtual void set_emitter(EmitterHandle emitter, const AudioTransform& transform) override;
		virtual void set_emitter_scale(EmitterHandle emitter, float scale) override;
		virtual void set_paused(bool paused) override;

		virtual PlayingId post_event(std::string_view event, EmitterHandle emitter) override;
		virtual void stop_playing(PlayingId playing) override;
		virtual void stop_emitter(EmitterHandle emitter) override;
		virtual void stop_all() override;

		virtual void set_rtpc(std::string_view name, float value, EmitterHandle emitter) override;
		virtual void set_switch(std::string_view group, std::string_view state, EmitterHandle emitter) override;
		virtual void set_state(std::string_view group, std::string_view state) override;

		virtual bool load_bank(std::string_view name, std::span<const std::byte> data, std::string& error) override;
		virtual void unload_bank(std::string_view name) override;
		virtual std::vector<std::string> loaded_banks() const override;
		virtual void note_bank_events(std::string_view bank, std::span<const std::string> events) override;

		virtual u32 playing_count() const override { return static_cast<u32>(m_Playing.size()); }

		// With strict on, an event no loaded bank named (note_bank_events) is warned about once per name and does not start. Off by default:
		// a Null backend with no metadata accepts every event.
		void set_strict(bool strict) { m_Strict = strict; }
		u32 unknown_warnings() const { return m_UnknownWarnings; }

		const std::vector<std::string>& calls() const { return m_Calls; }
		void clear_calls() { m_Calls.clear(); }
		// The log keeps the newest calls: past MAX_CALLS the oldest half goes, so a Null backend that runs for hours does not grow without bound.
		static constexpr size_t MAX_CALLS = 8192;
		u32 emitter_count() const { return static_cast<u32>(m_Emitters.size()); }
		u32 update_count() const { return m_Updates; }
		const AudioTransform& listener() const { return m_Listener; }
		const AudioTransform& emitter_transform(EmitterHandle emitter) const { return m_EmitterTransforms.at(emitter.Value); }
		bool paused() const { return m_Paused; }
		bool has_emitter(EmitterHandle emitter) const { return m_Emitters.contains(emitter.Value); }

	private:
		void record(std::string call)
		{
			if (m_Calls.size() >= MAX_CALLS)
			{
				m_Calls.erase(m_Calls.begin(), m_Calls.begin() + static_cast<std::ptrdiff_t>(MAX_CALLS / 2));
			}
			m_Calls.push_back(std::move(call));
		}

		struct Playing { u64 Id = 0; u32 Emitter = 0; };

		std::vector<std::string> m_Calls;
		std::set<u32> m_Emitters;
		std::vector<Playing> m_Playing;
		std::set<std::string> m_Banks;
		std::unordered_set<std::string> m_KnownEvents;
		std::unordered_set<std::string> m_WarnedEvents;
		AudioTransform m_Listener{};
		std::map<u32, AudioTransform> m_EmitterTransforms;
		bool m_Paused = false;
		u32 m_NextEmitter = 1;
		u64 m_NextPlaying = 1;
		u32 m_Updates = 0;
		u32 m_UnknownWarnings = 0;
		bool m_Strict = false;
	};
}
