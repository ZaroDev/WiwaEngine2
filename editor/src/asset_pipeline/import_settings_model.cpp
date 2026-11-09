#include <editor_common.h>
#include "import_settings_model.h"

#include "asset_database.h"

namespace wiwa
{
	std::optional<ImportSettingsModel> ImportSettingsModel::open(AssetDatabase& database, const std::filesystem::path& source)
	{
		const IImporter* importer = database.importer_for(source);
		if (!importer || !importer->editable())
		{
			return std::nullopt;
		}
		std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
		if (!meta)
		{
			return std::nullopt;
		}

		ImportSettingsModel model;
		model.m_Source = source;
		model.m_ImporterName = importer->name();
		model.m_Schema = importer->option_schema();
		model.m_Saved = sanitize_settings(model.m_Schema, meta->Settings, &model.m_Problems);
		model.m_Values = model.m_Saved;
		return model;
	}

	bool ImportSettingsModel::differs_from_default(const std::string& key) const
	{
		for (const ImportOption& option : m_Schema)
		{
			if (option.Key == key)
			{
				return m_Values.contains(key) && m_Values[key] != option.Default;
			}
		}
		return false;
	}

	bool ImportSettingsModel::apply(AssetDatabase& database, std::string& error)
	{
		m_Values = sanitized();
		if (!database.store_import_settings(m_Source, m_Values, error))
		{
			return false;
		}
		m_Saved = m_Values;
		database.reimport_async({ m_Source });
		return true;
	}
}
