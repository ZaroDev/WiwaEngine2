#include <editor_common.h>
#include "import_option.h"

#include <algorithm>

namespace wiwa
{
	ImportOption ImportOption::make_bool(std::string key, std::string label, bool value, std::string tooltip)
	{
		ImportOption option;
		option.Key = std::move(key);
		option.Label = std::move(label);
		option.Tooltip = std::move(tooltip);
		option.Type = ImportOptionType::Bool;
		option.Default = value;
		return option;
	}

	ImportOption ImportOption::make_int(std::string key, std::string label, int value, int min, int max, std::string tooltip)
	{
		ImportOption option;
		option.Key = std::move(key);
		option.Label = std::move(label);
		option.Tooltip = std::move(tooltip);
		option.Type = ImportOptionType::Int;
		option.Default = value;
		option.Min = min;
		option.Max = max;
		return option;
	}

	ImportOption ImportOption::make_float(std::string key, std::string label, double value, double min, double max, std::string tooltip)
	{
		ImportOption option;
		option.Key = std::move(key);
		option.Label = std::move(label);
		option.Tooltip = std::move(tooltip);
		option.Type = ImportOptionType::Float;
		option.Default = value;
		option.Min = min;
		option.Max = max;
		return option;
	}

	ImportOption ImportOption::make_enum(std::string key, std::string label, std::string value, std::vector<std::string> choices, std::string tooltip)
	{
		ImportOption option;
		option.Key = std::move(key);
		option.Label = std::move(label);
		option.Tooltip = std::move(tooltip);
		option.Type = ImportOptionType::Enum;
		option.Default = std::move(value);
		option.Choices = std::move(choices);
		return option;
	}

	nlohmann::json schema_defaults(const std::vector<ImportOption>& schema)
	{
		nlohmann::json result = nlohmann::json::object();
		for (const ImportOption& option : schema)
		{
			result[option.Key] = option.Default;
		}
		return result;
	}

	nlohmann::json sanitize_settings(const std::vector<ImportOption>& schema, const nlohmann::json& in, std::vector<std::string>* problems)
	{
		nlohmann::json result = nlohmann::json::object();
		auto problem = [&](const ImportOption& option, const char* what)
		{
			if (problems)
			{
				problems->push_back(option.Label + ": " + what);
			}
		};

		for (const ImportOption& option : schema)
		{
			const bool present = in.is_object() && in.contains(option.Key);
			const nlohmann::json& value = present ? in[option.Key] : option.Default;
			nlohmann::json out = option.Default;

			switch (option.Type)
			{
			case ImportOptionType::Bool:
				if (value.is_boolean())
				{
					out = value;
				}
				else if (present)
				{
					problem(option, "not a boolean, using the default");
				}
				break;
			case ImportOptionType::Int:
				if (value.is_number())
				{
					const double clamped = std::clamp(value.get<double>(), option.Min, option.Max);
					out = static_cast<int>(std::lround(clamped));
					if (clamped != value.get<double>())
					{
						problem(option, "out of range, clamped");
					}
				}
				else if (present)
				{
					problem(option, "not a number, using the default");
				}
				break;
			case ImportOptionType::Float:
				if (value.is_number())
				{
					const double clamped = std::clamp(value.get<double>(), option.Min, option.Max);
					out = clamped;
					if (clamped != value.get<double>())
					{
						problem(option, "out of range, clamped");
					}
				}
				else if (present)
				{
					problem(option, "not a number, using the default");
				}
				break;
			case ImportOptionType::Enum:
				if (value.is_string() && std::find(option.Choices.begin(), option.Choices.end(), value.get<std::string>()) != option.Choices.end())
				{
					out = value;
				}
				else if (present)
				{
					problem(option, "not one of the choices, using the default");
				}
				break;
			case ImportOptionType::String:
				if (value.is_string())
				{
					out = value;
				}
				else if (present)
				{
					problem(option, "not text, using the default");
				}
				break;
			}
			result[option.Key] = out;
		}
		return result;
	}
}
