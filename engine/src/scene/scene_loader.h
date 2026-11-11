#pragma once

#include <functional>
#include <string>
#include <vector>

#include <assets/asset_data.h>
#include "prefab_expander.h"
#include "scene.h"
#include "scene_records.h"

namespace wiwa
{
	// Where an entity of a blueprint came from.
	struct EntityOrigin
	{
		EntityGuid Instance{}; // the instance that generated it; invalid for an entity written in the scene
		u64 NodeId = 0;        // its prefab node, or ADDED_CHILD_NODE (see prefab_expander.h)
		bool NestedRoot = false; // the root of a nested prefab instance
	};

	// A scene ready to be created: the file's entities with every prefab instance expanded, parents before children.
	// Built off the main thread, from data only.
	struct SceneBlueprint
	{
		std::string Name{};
		std::vector<EntityRecord> Entities{};
		std::vector<EntityOrigin> Origins{};     // parallel to Entities
		std::vector<AssetRef> References{};      // every asset the entities and the settings refer to
		SceneSettings Settings{};
		std::vector<std::string> Warnings{};
	};

	// Returns the prefab a PrefabInstance names, or null if it is not available (it did not load).
	using PrefabLookup = PrefabResolver;

	SceneBlueprint build_blueprint(const SceneData& data, const PrefabLookup& prefabs);

	// Creates the entities of a blueprint in a scene, a few at a time. Main thread.
	class SceneCommit
	{
	public:
		SceneCommit(Scene& target, SceneBlueprint blueprint);

		// Creates entities until budgetMs have passed (always at least one). True when all of them exist.
		bool step(float budgetMs);

		size_t total() const { return m_Blueprint.Entities.size(); }
		size_t created() const { return m_Next; }
		const ApplyReport& report() const { return m_Report; }
		const SceneBlueprint& blueprint() const { return m_Blueprint; }
		// Everything created so far, so that a cancelled additive load can take it out again.
		const std::vector<entt::entity>& entities() const { return m_Created; }

	private:
		Scene& m_Target;
		SceneBlueprint m_Blueprint;
		size_t m_Next = 0;
		ApplyReport m_Report{};
		std::vector<entt::entity> m_Created{};
	};
}
