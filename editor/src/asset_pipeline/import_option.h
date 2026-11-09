#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/common.h>

namespace wiwa
{
	enum class ImportOptionType
	{
		Bool,
		Int,
		Float,
		Enum,
		String
	};

	// One import setting an importer understands. The schema of an importer is the single description of its settings: the defaults
	// come from it, and the import dialog is drawn from it.
	struct ImportOption
	{
		std::string Key{};
		std::string Label{};
		std::string Tooltip{};
		ImportOptionType Type = ImportOptionType::Bool;
		nlohmann::json Default{};
		double Min = 0.0;                // Int and Float
		double Max = 0.0;                // Int and Float
		std::vector<std::string> Choices{}; // Enum

		static ImportOption make_bool(std::string key, std::string label, bool value, std::string tooltip = {});
		static ImportOption make_int(std::string key, std::string label, int value, int min, int max, std::string tooltip = {});
		static ImportOption make_float(std::string key, std::string label, double value, double min, double max, std::string tooltip = {});
		static ImportOption make_enum(std::string key, std::string label, std::string value, std::vector<std::string> choices, std::string tooltip = {});
	};

	// {key: default} of every option.
	nlohmann::json schema_defaults(const std::vector<ImportOption>& schema);

	// `in` as the schema wants it: every schema key present; a value of the wrong type, or an enum value that is not a choice, becomes the
	// default; numbers are clamped. Keys that are not in the schema are dropped. `problems` (optional) gets one line per value that was changed.
	nlohmann::json sanitize_settings(const std::vector<ImportOption>& schema, const nlohmann::json& in, std::vector<std::string>* problems = nullptr);
}
