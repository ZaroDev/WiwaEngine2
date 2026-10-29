#include <pch.h>
#include "scene_components.h"

#include "components.h"
#include "reflection/component_registry.h"
#include "reflection/reflect_macros.h"

namespace wiwa
{
	void register_scene_components()
	{
		ComponentRegistry& registry = ComponentRegistry::get();

		registry.register_component(WI_COMPONENT(Id, WI_FIELD(Guid)));
		registry.register_component(WI_COMPONENT(Name, WI_FIELD(Value)));
		registry.register_component(WI_COMPONENT(Transform, WI_FIELD(Position), WI_FIELD(Rotation), WI_FIELD(Scale)));
	}
}
