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

	const Ref<PhysicsSection>& section() const { return m_Section; }

private:
	void settings_tab();
	void stats_tab();

	wiwa::SceneEditor& m_Editor;
	Ref<PhysicsSection> m_Section;
};
