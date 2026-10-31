#include <pch.h>
#include "script_component.h"

#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>

namespace wiwa
{
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
