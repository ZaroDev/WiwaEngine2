#include <pch.h>
#include "script_component.h"

#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>

namespace wiwa
{
	namespace
	{
		constexpr std::string_view PREFIX = "script:";
	}

	std::string script_component_name(AssetGuid guid)
	{
		return std::string(PREFIX) + guid.to_string();
	}

	std::optional<AssetGuid> script_guid_from_component(std::string_view name)
	{
		if (!name.starts_with(PREFIX))
		{
			return std::nullopt;
		}
		return AssetGuid::from_string(name.substr(PREFIX.size()));
	}

	void register_script_component(const ScriptClass& scriptClass, entt::registry* reconcile)
	{
		ComponentInfo info = make_value_bag_info(script_component_name(scriptClass.Asset.Guid), scriptClass.Properties);
		info.DisplayName = scriptClass.Name;

		ComponentRegistry& registry = ComponentRegistry::get();
		registry.register_component(std::move(info));

		if (reconcile != nullptr)
		{
			if (const ComponentInfo* registered = registry.find(script_component_name(scriptClass.Asset.Guid)))
			{
				reconcile_bag(*reconcile, *registered);
			}
		}
	}
}
