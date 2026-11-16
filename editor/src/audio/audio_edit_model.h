#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <audio/audio_bank_info.h>

// What the Audio panel, the event picker of the inspector and the emitter gizmo ask about the loaded banks, without ImGui so that the smoke check can
// ask them too.
namespace wiwa
{
	struct AudioEventRow
	{
		std::string Name{};
		float MaxAttenuation = 0.0f;
		float DurationMs = 0.0f;
		bool Looping = false;
	};

	// Every event of the banks (each name once), sorted by name, whose name contains the filter (case-insensitive; empty keeps all).
	std::vector<AudioEventRow> audio_event_rows(const std::vector<const AudioBankInfo*>& banks, std::string_view filter);

	// True if the name is not empty, at least one bank has metadata, and none lists the event. With no metadata nothing can be told, so nothing is flagged.
	bool audio_event_unknown(std::string_view event, const std::vector<const AudioBankInfo*>& banks);

	// The distance the event is heard from, scaled as the emitter scales it; 0 for an unknown event or one without attenuation.
	float audio_attenuation_radius(std::string_view event, float scale, const std::vector<const AudioBankInfo*>& banks);
}
