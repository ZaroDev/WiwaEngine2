#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <scene/scene_data.h>

// The source format of a scene (.wscene): JSON that is meant to be read, diffed and edited by hand. Values of components this
// engine knows are plain JSON (an enum by name, a vector as an array). A value it has no description of is written as
// {"$type": "float", "value": 1.0}, so it survives a save exactly.
namespace wiwa
{
	// Ordered, so that the file keeps the order of components and fields.
	using SceneJson = nlohmann::ordered_json;

	struct SceneJsonResult
	{
		std::optional<SceneData> Scene{};
		std::string Error{}; // when Scene is empty: what is wrong and where, like "entities[12].MeshRenderer.Mesh: not an AssetRef"
		std::vector<std::string> Warnings{};
	};

	// Validates as it reads: ids that are guids and unique, parents that exist, no parent loops, values of the right type. The
	// entities come out parents first (depth first, siblings in file order). Thread-safe.
	SceneJsonResult scene_from_json(const SceneJson& json);

	SceneJson scene_to_json(const SceneData& data);
}
