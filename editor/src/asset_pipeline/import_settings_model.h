#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "import_option.h"

namespace wiwa
{
	class AssetDatabase;

	// What the import dialog edits (no ImGui): the options of one asset's importer with the values the user is changing. The values start as what
	// the .meta holds (missing, mistyped or out of range entries fixed by the schema); nothing is written until apply().
	class ImportSettingsModel
	{
	public:
		// nullopt if the file has no editable importer or no readable .meta.
		static std::optional<ImportSettingsModel> open(AssetDatabase& database, const std::filesystem::path& source);

		const std::filesystem::path& source() const { return m_Source; }
		const std::string& importer_name() const { return m_ImporterName; }
		const std::vector<ImportOption>& schema() const { return m_Schema; }

		// The working values, one entry per schema key. The dialog edits them in place.
		nlohmann::json& values() { return m_Values; }
		const nlohmann::json& values() const { return m_Values; }

		// Lines about entries of the .meta the schema had to fix when it was opened.
		const std::vector<std::string>& open_problems() const { return m_Problems; }

		bool dirty() const { return sanitized() != m_Saved; }
		bool differs_from_default(const std::string& key) const;
		void revert() { m_Values = m_Saved; }
		void set_defaults() { m_Values = schema_defaults(m_Schema); }

		// Stores the values in the .meta and starts cooking the file again on the worker pool (the editor shows the progress; a failed import is
		// reported there). False with error if the values could not be stored.
		bool apply(AssetDatabase& database, std::string& error);

	private:
		nlohmann::json sanitized() const { return sanitize_settings(m_Schema, m_Values); }

		std::filesystem::path m_Source{};
		std::string m_ImporterName{};
		std::vector<ImportOption> m_Schema{};
		nlohmann::json m_Values = nlohmann::json::object();
		nlohmann::json m_Saved = nlohmann::json::object();
		std::vector<std::string> m_Problems{};
	};
}
