#pragma once

// The Wwise audio backend. Built only with WI_AUDIO_WWISE (premake defines it when WWISESDK is set). No Wwise header is included from here: the
// Wwise types live behind WwiseState in wwise_backend.cpp, and nothing outside engine/src/audio/wwise includes AK headers.
#ifdef WI_AUDIO_WWISE

#include <memory>

#include <audio/audio_backend.h>

namespace wiwa
{
	struct WwiseState;

	class WwiseAudioBackend final : public IAudioBackend
	{
	public:
		WwiseAudioBackend();
		virtual ~WwiseAudioBackend() override;

		virtual AudioBackendKind kind() const override { return AudioBackendKind::Wwise; }
		virtual bool init(const AudioInitInfo& info, std::string& error) override;
		virtual void shutdown() override;
		virtual void update() override;

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

		virtual u32 playing_count() const override;

	private:
		std::unique_ptr<WwiseState> m_State;
	};
}

#endif
