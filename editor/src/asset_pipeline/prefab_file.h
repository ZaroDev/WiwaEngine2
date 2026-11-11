#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <assets/asset_data.h>
#include <scene/scene_data.h>

// The source format of a prefab made from scene entities (.wprefab): the .wscene JSON of the entities of a subtree, parents first. The
// importer turns it into a PrefabData whose nodes hold the entities' components.
namespace wiwa
{
	// One node per entity. Name and Transform become the node's own fields, every other component goes to PrefabNode::Components. An
	// entity with a PrefabInstance becomes a node that holds a nested instance (the prefab it names and its overrides; the instance's
	// generated children are not in the list). A parent that is not in the list, a PrefabInstance that names no prefab, or an empty
	// list is refused, with the reason in error.
	std::optional<PrefabData> prefab_from_records(const std::vector<EntityRecord>& entities, std::string name, std::string& error);

	// Writes through a temporary file that is renamed over the target. False (and logs) if it cannot.
	bool write_prefab_file(const std::filesystem::path& file, const std::vector<EntityRecord>& entities, const std::string& name);

	// nullopt, with the reason in error, if the file cannot be read or is not a valid scene file.
	std::optional<std::vector<EntityRecord>> read_prefab_file(const std::filesystem::path& file, std::string& error, std::vector<std::string>* warnings = nullptr);
}
