#pragma once

#include <assets/asset_manager.h>
#include <scene/scene_editor.h>

#include <filesystem>
#include <functional>
#include <string>

namespace wiwa
{
	// The scene a project made by the launcher starts with (launcher.md): Plane, Directional light and Camera from the entity templates, saved as
	// assets/scenes/main.wscene and made the build settings' default (and only) scene. The launcher cannot write it: a scene names assets by guid, and
	// the shapes model only gets one when the editor imports it.
	inline constexpr const char* STARTER_SCENE = "assets/scenes/main.wscene";
	inline constexpr const char* STARTER_SCENE_ASSET = "Assets/scenes/main.wscene";

	enum class StarterSceneStep
	{
		NotNeeded, // no NEW_PROJECT_MARKER in the project
		NotYet,    // waiting: for the first scan, for the shapes model to be in the catalog, or for the saved scene to be imported
		Done,      // the scene is saved and the default scene; the marker is gone
		Failed     // logged once; the marker stays, so the next start tries again
	};

	// The time the editor keeps calling a step that says NotYet after the first scan finished; then it gives up (Failed, the marker stays).
	inline constexpr float STARTER_SCENE_WAIT_SECONDS = 15.0f;

	// One step of it; the editor calls it every frame until it stops returning NotYet. `import` imports a file the step wrote (the editor passes the
	// database's reimport). Every path is under `projectDir`. Main thread.
	StarterSceneStep build_starter_scene(const std::filesystem::path& projectDir, bool scanFinished, AssetManager& assets, SceneEditor& editor,
		const std::function<void(const std::filesystem::path&)>& import, std::string& error);
}
