#pragma once

#include <string>

#include "panel.h"

#include <scene/reflection/component_info.h>
#include <scene/scene_editor.h>

// The "Inspector" panel: the components of the selected entity, drawn from their field descriptions (one widget per field type), with
// Add/Remove component. On an entity of a prefab instance, a changed field is marked as overridden and has a button to go back to the
// prefab's value. Main thread, between the ImGui frame's begin and end.
class InspectorPanel : public Panel
{
public:
	explicit InspectorPanel(wiwa::SceneEditor& editor) : m_Editor(editor), Panel("Inspector", PanelCategory::Window) {}

	virtual void on_draw() override;

private:
	void draw_component(entt::entity entity, const wiwa::ComponentInfo& info);
	// true if the user changed the value (and the new one is in value)
	bool draw_field(entt::entity entity, const wiwa::ComponentInfo& info, size_t index, const wiwa::FieldValue& current, wiwa::FieldValue& value);

	wiwa::SceneEditor& m_Editor;
	std::function<void()> m_Deferred{};

	// A quaternion is edited as Euler angles (degrees); this keeps the angles the user typed while the quaternion they make is the
	// one on the entity, so that they do not jump around.
	std::string m_EulerKey{};
	glm::vec3 m_Euler{ 0.0f };
	glm::quat m_EulerQuat{ 1.0f, 0.0f, 0.0f, 0.0f };
};
