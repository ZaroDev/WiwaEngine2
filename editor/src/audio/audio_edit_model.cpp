#include <editor_common.h>
#include "audio_edit_model.h"

#include <algorithm>
#include <cctype>
#include <set>

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
	}

	std::vector<AudioEventRow> audio_event_rows(const std::vector<const AudioBankInfo*>& banks, std::string_view filter)
	{
		const std::string needle = lower(filter);
		std::vector<AudioEventRow> rows;
		std::set<std::string> seen;
		for (const AudioBankInfo* bank : banks)
		{
			for (const AudioEventInfo& event : bank->Events)
			{
				if ((needle.empty() || lower(event.Name).find(needle) != std::string::npos) && seen.insert(event.Name).second)
				{
					rows.push_back({ event.Name, event.MaxAttenuation, event.DurationMs, event.Looping });
				}
			}
		}
		std::sort(rows.begin(), rows.end(), [](const AudioEventRow& a, const AudioEventRow& b) { return a.Name < b.Name; });
		return rows;
	}

	bool audio_event_unknown(std::string_view event, const std::vector<const AudioBankInfo*>& banks)
	{
		if (event.empty() || banks.empty())
		{
			return false;
		}
		return std::none_of(banks.begin(), banks.end(), [&](const AudioBankInfo* bank) { return bank->find_event(event) != nullptr; });
	}

	float audio_attenuation_radius(std::string_view event, float scale, const std::vector<const AudioBankInfo*>& banks)
	{
		for (const AudioBankInfo* bank : banks)
		{
			if (const AudioEventInfo* info = bank->find_event(event))
			{
				return info->MaxAttenuation > 0.0f ? info->MaxAttenuation * scale : 0.0f;
			}
		}
		return 0.0f;
	}
}
