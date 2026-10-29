#pragma once

#include <string>
#include <vector>

#include "entity_guid.h"
#include "reflection/field_value.h"

// The neutral description of a scene: entities, their components and the value of each field, by name. The scene file
// (JSON), the cooked SceneData asset, prefab expansion and the editor's Save all speak this, so none of them needs the
// C++ component types.
namespace wiwa
{
	struct FieldRecord
	{
		std::string Name{};
		FieldValue Value{};
	};

	struct ComponentRecord
	{
		std::string Name{};
		std::vector<FieldRecord> Fields{};
	};

	struct PrefabOverride;

	struct EntityRecord
	{
		EntityGuid Id{};
		EntityGuid Parent{}; // invalid for a top-level entity
		std::vector<ComponentRecord> Components{};
		std::vector<PrefabOverride> Overrides{}; // only on the entity that holds a PrefabInstance
	};

	enum class OverrideKind : u32
	{
		SetField,
		AddComponent,
		RemoveComponent,
		AddChild,
		RemoveNode,
	};

	// One change to a node of a linked prefab instance. NodeId is the stable id of the Prefab node (0 is the instance root).
	struct PrefabOverride
	{
		OverrideKind Kind = OverrideKind::SetField;
		u64 NodeId = 0;
		std::string Component{}; // SetField, AddComponent (by Added.Name), RemoveComponent
		std::string Field{};     // SetField
		FieldValue Value{};      // SetField
		ComponentRecord Added{}; // AddComponent
		EntityRecord Child{};    // AddChild: its Parent is the guid of the node it goes under
	};

	struct SceneData
	{
		std::string Name{};
		std::vector<EntityRecord> Entities{};
	};
}
