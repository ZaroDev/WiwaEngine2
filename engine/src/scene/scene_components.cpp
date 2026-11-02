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

		registry.register_component(WI_COMPONENT(Camera,
			WI_FIELD_META(Projection, FieldMeta::enumeration({ "Perspective", "Orthographic" })),
			WI_FIELD_META(FovY, FieldMeta::range(1.0f, 179.0f)),
			WI_FIELD(Near),
			WI_FIELD(Far),
			WI_FIELD(OrthoSize),
			WI_FIELD(Primary)));

		registry.register_component(WI_COMPONENT(Light,
			WI_FIELD_META(Kind, FieldMeta::enumeration({ "Directional", "Point", "Spot" })),
			WI_FIELD_META(Color, FieldMeta::color()),
			WI_FIELD(Intensity),
			WI_FIELD(Range),
			WI_FIELD(InnerAngle),
			WI_FIELD(OuterAngle),
			WI_FIELD(CastsShadows)));

		registry.register_component(WI_COMPONENT(MeshRenderer,
			WI_FIELD_META(Mesh, FieldMeta::asset(AssetType::Mesh)),
			WI_FIELD_META(Materials, FieldMeta::asset(AssetType::Material))));

		// The overrides are not a field: the record layer saves and applies them.
		registry.register_component(WI_COMPONENT(PrefabInstance,
			WI_FIELD_META(Prefab, FieldMeta::asset(AssetType::Prefab))));

		registry.register_component(WI_COMPONENT(UICanvas,
			WI_FIELD_META(Panel, FieldMeta::asset(AssetType::UiPanel)),
			WI_FIELD(SortOrder),
			WI_FIELD(Visible)));
	}
}
