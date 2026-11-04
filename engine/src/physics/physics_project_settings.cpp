#include <pch.h>
#include "physics_project_settings.h"

namespace wiwa
{
	void archive(BinaryArchive& ar, PhysicsLayers& v)
	{
		u32 version = PHYSICS_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != PHYSICS_SETTINGS_VERSION)
		{
			ar.fail();
			return;
		}
		for (std::string& name : v.Names)
		{
			ar.string(name);
		}
		for (u32& row : v.Matrix)
		{
			ar.value(row);
		}
	}

	std::optional<PhysicsLayers> read_physics_layers(IAssetSource& source)
	{
		if (!source.contains(PHYSICS_SETTINGS_GUID))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(PHYSICS_SETTINGS_GUID);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<PhysicsLayers>(*bytes);
	}
}
