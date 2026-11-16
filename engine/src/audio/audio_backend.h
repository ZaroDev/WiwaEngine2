#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio_types.h"

namespace wiwa
{
	// What an audio vendor implements. Events, parameters (RTPC), switches, states and banks are the model both Wwise and FMOD share.
	// Main thread only: the backend may run its own audio thread but the module calls it from main. Names are the vendor's own
	// (a Wwise event name); a name the backend does not know is logged once and the call does nothing.
	class IAudioBackend
	{
	public:
		virtual ~IAudioBackend() = default;

		virtual AudioBackendKind kind() const = 0;

		// False, and why in error, if the vendor engine could not start. The backend is unusable then.
		virtual bool init(const AudioInitInfo& info, std::string& error) = 0;
		virtual void shutdown() = 0;
		// Once per frame.
		virtual void update() = 0;

		virtual void set_listener(const AudioTransform& transform) = 0;

		virtual EmitterHandle create_emitter(std::string_view debugName) = 0;
		virtual void destroy_emitter(EmitterHandle emitter) = 0; // stops what it plays
		virtual void set_emitter(EmitterHandle emitter, const AudioTransform& transform) = 0;
		// Multiplies the attenuation distances of what the emitter plays (1 = as authored).
		virtual void set_emitter_scale(EmitterHandle emitter, float scale) = 0;

		// Suspends or resumes all sound (the game is paused).
		virtual void set_paused(bool paused) = 0;

		// An invalid emitter plays the event globally (not positioned). An invalid PlayingId means the event was not started.
		virtual PlayingId post_event(std::string_view event, EmitterHandle emitter) = 0;
		virtual void stop_playing(PlayingId playing) = 0;
		virtual void stop_emitter(EmitterHandle emitter) = 0;
		virtual void stop_all() = 0;

		// An invalid emitter is the global scope.
		virtual void set_rtpc(std::string_view name, float value, EmitterHandle emitter) = 0;
		virtual void set_switch(std::string_view group, std::string_view state, EmitterHandle emitter) = 0;
		virtual void set_state(std::string_view group, std::string_view state) = 0;

		// The bytes are copied if the vendor needs them to outlive the call.
		virtual bool load_bank(std::string_view name, std::span<const std::byte> data, std::string& error) = 0;
		virtual void unload_bank(std::string_view name) = 0;
		virtual std::vector<std::string> loaded_banks() const = 0;

		// The event names a loaded bank contains, from its metadata. Backends that cannot tell on their own use it to tell known names from typos.
		virtual void note_bank_events(std::string_view bank, std::span<const std::string> events) { (void)bank; (void)events; }

		virtual u32 playing_count() const = 0;
	};
}
