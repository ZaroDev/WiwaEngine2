#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include <assets/asset_data.h>
#include <assets/asset_manager.h>
#include <assets/handle.h>
#include <scene/scene.h>

#include "nav_bake.h"

namespace wiwa
{
	// What a NavMeshVolume bakes, taken from the scene in two steps so that a bake can run on a worker thread: capture_nav_snapshot reads the scene (main
	// thread, cheap: a matrix and a mesh reference per renderer), build_nav_input loads the meshes and makes the triangles (worker thread).
	struct NavSnapshotItem
	{
		AssetRef Mesh{};
		glm::mat4 World{ 1.0f };
		u8 Area = static_cast<u8>(NavArea::Walkable);
	};

	struct NavSnapshot
	{
		std::vector<NavSnapshotItem> Items{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		NavBuildSettings Build{};
	};

	// Where the meshes of a snapshot come from. get() may block (a worker thread asks); null for a mesh that is not there or failed to load.
	class INavMeshSource
	{
	public:
		virtual ~INavMeshSource() = default;
		virtual const MeshData* get(const AssetRef& ref, const std::atomic<bool>* cancel) = 0;
	};

	// The meshes of the asset manager: loads them and waits for those loads only (never for every load pending in the editor), up to a minute each.
	class AssetNavMeshSource final : public INavMeshSource
	{
	public:
		explicit AssetNavMeshSource(AssetManager& assets) : m_Assets(assets) {}
		virtual const MeshData* get(const AssetRef& ref, const std::atomic<bool>* cancel) override;

	private:
		AssetManager& m_Assets;
		std::unordered_map<AssetGuid, Handle<MeshData>> m_Handles{};
	};

	// The volume's box (Size around its world position, axis aligned) and settings, and the MeshRenderers whose bounds are not known to miss it, each
	// with the area of its entity's NavAreaTag (or the nearest ancestor's; Walkable when there is none). An entity with a Collider contributes only when its
	// Collider::Layer is a bit of the volume's LayerMask (a mesh without a collider always contributes). The meshes of an agent (an entity with a NavAgent and
	// its descendants) are left out: they walk on the ground, they are not part of it. The volume's world transform is brought up to date
	// first. An entity that is not a NavMeshVolume gives an empty snapshot and error says so. Main thread.
	NavSnapshot capture_nav_snapshot(Scene& scene, entt::entity volume, std::string* error = nullptr);

	// The world-space triangles of the snapshot that touch its box. A skinned mesh is taken in its bind pose; a mesh the source cannot give is left out and
	// logged. Stops early (with what it has) when `cancel` is set. error says "no geometry touches the volume" when there is none.
	NavBakeInput build_nav_input(const NavSnapshot& snapshot, INavMeshSource& meshes, const std::atomic<bool>* cancel = nullptr, std::string* error = nullptr);

	// Both steps in one, for callers that wait (a command line bake).
	NavBakeInput gather_nav_input(Scene& scene, entt::entity volume, AssetManager& assets, std::string* error = nullptr);
}
