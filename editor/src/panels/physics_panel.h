#pragma once

#include <filesystem>
#include <string>

#include "panel.h"

#include <scene/scene_editor.h>

#include "../settings/physics_section.h"

namespace wiwa
{
	class PhysicsModule;
}

// The "Physics" panel. Settings: the scene's step settings (gravity, rate, iterations, sleep threshold), edited through the scene editor like
// the World panel's rows (undo, saved with the scene). Stats: bodies, awake bodies, the cost of a step, steps per frame and dropped steps.
// The project's layer names and collision matrix are edited in the Settings panel; the PhysicsSection that holds them is made here and shared.
// Main thread, between the ImGui frame's begin and end.
class PhysicsPanel final : public Panel
{
public:
	// Reads the layers from `layersFile` (the default layers if there is no file) and gives them to the physics module.
	PhysicsPanel(wiwa::SceneEditor& editor, std::filesystem::path layersFile);

	virtual void on_draw() override;

	const wiwa::PhysicsLayers& layers() const { return m_Section->layers(); }
	// What a click in the matrix does (PhysicsSection): sets the pair, saves the file and updates the world. False (and why in last_error) if the save failed.
	bool set_collides(wiwa::u32 a, wiwa::u32 b, bool collides) { return m_Section->set_collides(a, b, collides); }
	bool set_layer_name(wiwa::u32 layer, const std::string& name) { return m_Section->set_layer_name(layer, name); }
	const std::string& last_error() const { return m_Section->last_error(); }
	const Ref<PhysicsSection>& section() const { return m_Section; }
	wiwa::u32 frames_drawn() const { return m_Frames; }
	// Brings the window to the front of its dock on the next frame (the smoke check wants the contents drawn).
	void request_focus() { m_Focus = true; }

private:
	void settings_tab();
	void stats_tab();

	wiwa::SceneEditor& m_Editor;
	Ref<PhysicsSection> m_Section;
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;
};
