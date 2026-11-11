#pragma once

#include <string>

#include <entt/entt.hpp>

#include <assets/asset_manager.h>
#include <scene/scene.h>

#include "nav_bake.h"

namespace wiwa
{
	// What a NavMeshVolume bakes: the world-space triangles of the MeshRenderers whose bounds touch the volume's box (Size around the entity's world
	// position, axis aligned), each with the area of its entity's NavAreaTag (or the nearest ancestor's; Walkable when there is none). An entity with
	// a Collider contributes only when its Collider::Layer is a bit of the volume's LayerMask (a mesh without a collider always contributes). A
	// skinned mesh is taken in its bind pose. Blocks until the meshes have loaded; one that fails to load is left out and logged. The volume's world
	// transform is brought up to date first. With the entity not a NavMeshVolume the input is empty and error says so.
	NavBakeInput gather_nav_input(Scene& scene, entt::entity volume, AssetManager& assets, std::string* error = nullptr);
}
