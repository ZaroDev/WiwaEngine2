#pragma once

#include <functional>
#include <string>
#include <vector>

#include <assets/asset_manager.h>
#include <scene/scene_editor.h>

// Premade objects for the Hierarchy's Create menu and the command palette: an entity with components already attached and their fields
// set. Creating one is one undo step (the edits of one frame settle together). Main thread.
struct EntityTemplate
{
	std::string Name{};
	std::string Category{}; // the submenu ("3D", "Light", ...); empty: at the top of the menu
	// Creates it under parent (entt::null: top level); entt::null when nothing can be created there.
	std::function<entt::entity(wiwa::SceneEditor&, entt::entity parent)> Create{};
};

// The templates the project can make now. The 3D shapes come from "Assets/models/shapes/shapes.glb" (mesh/N has the local id N + 1: cube
// 1, sphere 2, cylinder 3, cone 4, torus 5, plane 6), so they are only listed while that model is in the catalog. The engine has no
// built-in primitive meshes, and there is no capsule mesh in the shapes model, so there is no capsule template.
std::vector<EntityTemplate> entity_templates(wiwa::AssetManager& assets);
