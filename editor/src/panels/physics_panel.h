#pragma once

#include <filesystem>
#include <string>

#include "panel.h"

#include <physics/physics_layers.h>
#include <scene/scene_editor.h>

namespace wiwa
{
	class PhysicsModule;
}

// The "Physics" panel. Settings: the scene's step settings (gravity, rate, iterations, sleep threshold), edited through the scene editor like
// the World panel's rows (undo, saved with the scene). Layers: the project's layer names and collision matrix, saved to physics.wsettings at
// once and applied to the world that runs. Stats: bodies, awake bodies, the cost of a step, steps per frame and dropped steps.
// Main thread, between the ImGui frame's begin and end.
class PhysicsPanel final : public Panel
{
public:
	// Reads the layers from `layersFile` (the default layers if there is no file) and gives them to the physics module.
	PhysicsPanel(wiwa::SceneEditor& editor, std::filesystem::path layersFile);

	virtual void on_draw() override;

	const wiwa::PhysicsLayers& layers() const { return m_Layers; }
	// What a click in the matrix does: sets the pair, saves the file and updates the world. False (and why in last_error) if the save failed.
	bool set_collides(wiwa::u32 a, wiwa::u32 b, bool collides);
	bool set_layer_name(wiwa::u32 layer, const std::string& name);
	const std::string& last_error() const { return m_Error; }
	wiwa::u32 frames_drawn() const { return m_Frames; }

private:
	void settings_tab();
	void layers_tab();
	void stats_tab();
	bool save_layers();

	wiwa::SceneEditor& m_Editor;
	std::filesystem::path m_File;
	wiwa::PhysicsLayers m_Layers{};
	std::string m_Error{};
	wiwa::u32 m_Frames = 0;
};
