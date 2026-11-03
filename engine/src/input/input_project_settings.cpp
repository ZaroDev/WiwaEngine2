#include <pch.h>
#include "input_project_settings.h"

namespace wiwa
{
	void archive(BinaryArchive& ar, InputProjectSettings& v)
	{
		u32 version = INPUT_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != INPUT_SETTINGS_VERSION)
		{
			ar.fail();
			return;
		}
		ar.value(v.DefaultMap);
		if (ar.reading() && ar.remaining() != 0)
		{
			ar.fail();
		}
	}

	std::optional<InputProjectSettings> read_input_settings(IAssetSource& source)
	{
		if (!source.contains(INPUT_SETTINGS_GUID))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(INPUT_SETTINGS_GUID);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<InputProjectSettings>(*bytes);
	}
}
