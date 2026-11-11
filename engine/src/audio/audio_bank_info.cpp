#include <pch.h>
#include "audio_bank_info.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace wiwa
{
	namespace
	{
		bool iequals(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
			{
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// Wwise writes numbers as strings ("100") or numbers, depending on the version.
		float number_of(const nlohmann::json& object, const char* key)
		{
			const auto it = object.find(key);
			if (it == object.end())
			{
				return 0.0f;
			}
			if (it->is_number())
			{
				return it->get<float>();
			}
			if (it->is_string())
			{
				try
				{
					return std::stof(it->get<std::string>());
				}
				catch (...)
				{
				}
			}
			return 0.0f;
		}

		std::string name_of(const nlohmann::json& object)
		{
			const auto it = object.find("Name");
			return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
		}

		template<typename T>
		void add_unique(std::vector<T>& list, T value)
		{
			if (std::find(list.begin(), list.end(), value) == list.end())
			{
				list.push_back(std::move(value));
			}
		}

		void add_group(std::vector<AudioNameGroup>& groups, const nlohmann::json& group, const char* valuesKey)
		{
			AudioNameGroup entry;
			entry.Name = name_of(group);
			if (entry.Name.empty())
			{
				return;
			}
			const auto values = group.find(valuesKey);
			if (values != group.end() && values->is_array())
			{
				for (const nlohmann::json& value : *values)
				{
					if (const std::string name = name_of(value); !name.empty())
					{
						entry.Values.push_back(name);
					}
				}
			}
			const auto existing = std::find_if(groups.begin(), groups.end(), [&](const AudioNameGroup& g) { return g.Name == entry.Name; });
			if (existing == groups.end())
			{
				groups.push_back(std::move(entry));
			}
		}

		void read_names(std::vector<std::string>& into, const nlohmann::json& bank, const char* key)
		{
			const auto list = bank.find(key);
			if (list != bank.end() && list->is_array())
			{
				for (const nlohmann::json& item : *list)
				{
					if (const std::string name = name_of(item); !name.empty())
					{
						add_unique(into, name);
					}
				}
			}
		}
	}

	std::optional<AudioBankInfo> parse_soundbanks_info(std::string_view json, std::string_view bankName, std::string& error)
	{
		const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not valid JSON";
			return std::nullopt;
		}

		const auto info = root.find("SoundBanksInfo");
		if (info == root.end() || !info->is_object())
		{
			error = "no SoundBanksInfo object";
			return std::nullopt;
		}
		const auto banks = info->find("SoundBanks");
		if (banks == info->end() || !banks->is_array())
		{
			error = "no SoundBanks list";
			return std::nullopt;
		}

		AudioBankInfo result;
		bool found = false;
		for (const nlohmann::json& bank : *banks)
		{
			if (!bank.is_object())
			{
				continue;
			}
			const auto shortName = bank.find("ShortName");
			const std::string name = shortName != bank.end() && shortName->is_string() ? shortName->get<std::string>() : std::string();
			if (!bankName.empty() && !iequals(name, bankName))
			{
				continue;
			}
			found = true;

			const auto events = bank.find("Events");
			if (events != bank.end() && events->is_array())
			{
				for (const nlohmann::json& event : *events)
				{
					AudioEventInfo entry;
					entry.Name = name_of(event);
					if (entry.Name.empty() || result.find_event(entry.Name))
					{
						continue;
					}
					entry.MaxAttenuation = number_of(event, "MaxAttenuation");
					entry.DurationMs = number_of(event, "DurationMax") * 1000.0f;
					const auto type = event.find("DurationType");
					entry.Looping = type != event.end() && type->is_string() && type->get<std::string>() == "Infinite";
					result.Events.push_back(std::move(entry));
				}
			}

			read_names(result.Rtpcs, bank, "GameParameters");
			read_names(result.Buses, bank, "Buses");

			const auto switches = bank.find("SwitchGroups");
			if (switches != bank.end() && switches->is_array())
			{
				for (const nlohmann::json& group : *switches)
				{
					add_group(result.Switches, group, "GroupValues");
				}
			}
			const auto states = bank.find("StateGroups");
			if (states != bank.end() && states->is_array())
			{
				for (const nlohmann::json& group : *states)
				{
					add_group(result.States, group, "States");
				}
			}
		}

		if (!found)
		{
			error = bankName.empty() ? std::string("no banks listed") : "bank '" + std::string(bankName) + "' is not listed";
			return std::nullopt;
		}
		return result;
	}
}
