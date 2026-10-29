#pragma once

#include <string>
#include <vector>

#include "scene.h"
#include "scene_data.h"

// Moves a scene between the registry and the neutral records (scene_data.h), through the ComponentRegistry. Main thread only,
// because it touches the registry.
namespace wiwa
{
	struct ApplyReport
	{
		u32 UnknownComponents = 0; // components in the records that this engine has no description of
		u32 UnknownFields = 0;     // fields, of known components, that it has none of
		std::vector<std::string> Warnings{};
	};

	// Everything reflected about the entity, plus what it kept of unknown data and its prefab overrides.
	EntityRecord record_from_entity(Scene& scene, entt::entity entity);

	// Creates the entity of a record, under the entity named by record.Parent (which has to exist already, or the entity goes to the
	// top level with a warning). entt::null, with a warning, if the guid is invalid or already in the scene.
	entt::entity apply_record(Scene& scene, const EntityRecord& record, ApplyReport* report = nullptr);

	// Every entity, parents before children, siblings in order.
	SceneData data_from_scene(Scene& scene, std::string name);
	ApplyReport apply_scene_data(Scene& scene, const SceneData& data);

	// Every asset the scene refers to, once each (the prefab of an instance, meshes, materials, and the ones inside overrides).
	std::vector<AssetRef> collect_references(const SceneData& data);
}
