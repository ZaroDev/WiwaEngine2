#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/common.h>

namespace wiwa
{
	struct AudioEventInfo
	{
		std::string Name{};
		float MaxAttenuation = 0.0f; // world units; 0 when the event has none (or the vendor does not say)
		float DurationMs = 0.0f;     // 0 when unknown
		bool Looping = false;
		bool operator==(const AudioEventInfo&) const = default;
	};

	// A switch group or a state group and its values.
	struct AudioNameGroup
	{
		std::string Name{};
		std::vector<std::string> Values{};
		bool operator==(const AudioNameGroup&) const = default;
	};

	// What a soundbank contains, from the vendor's metadata (Wwise: SoundbanksInfo.json). The editor reads it for the event browser and the
	// attenuation gizmo; the game never parses JSON, it gets this cooked into the bank's asset.
	struct AudioBankInfo
	{
		std::vector<AudioEventInfo> Events{};
		std::vector<std::string> Rtpcs{};
		std::vector<AudioNameGroup> Switches{};
		std::vector<AudioNameGroup> States{};
		std::vector<std::string> Buses{};

		const AudioEventInfo* find_event(std::string_view name) const
		{
			for (const AudioEventInfo& event : Events)
			{
				if (event.Name == name)
				{
					return &event;
				}
			}
			return nullptr;
		}

		bool operator==(const AudioBankInfo&) const = default;
	};

	// The metadata of Wwise's SoundbanksInfo.json for the bank with that short name (an empty name merges every bank in the file).
	// nullopt, and why in error, if the text is not that JSON; a bank the file does not list gives nullopt too.
	std::optional<AudioBankInfo> parse_soundbanks_info(std::string_view json, std::string_view bankName, std::string& error);
}
