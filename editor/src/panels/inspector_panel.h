#pragma once

#include <functional>
#include <string>

#include "panel.h"
#include "asset_field.h"

#include <scene/reflection/component_info.h>
#include <asset_pipeline/asset_database.h>
#include <scene/scene_editor.h>

// The "Inspector" panel: the components of the selected entity, drawn from their field descriptions (one widget per field type), with
// Add/Remove component. On an entity of a prefab instance, a changed field is marked as overridden and has a button to go back to the
// prefab's value. Main thread, between the ImGui frame's begin and end.
class InspectorPanel : public Panel
{
public:
	InspectorPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database);

	virtual void on_draw() override;

	// What the "Locate" button of an asset list slot calls (the editor points it at the Asset Browser).
	void set_locate(std::function<void(const wiwa::AssetRef&)> locate) { m_Locate = std::move(locate); }
	// What "Find references" in the menu of an asset slot calls.
	void set_find_references(std::function<void(const wiwa::AssetRef&)> find) { m_FindReferences = std::move(find); }
	void set_open_in_asset_editor(std::function<void(const wiwa::AssetRef&)> open) { m_OpenInAssetEditor = std::move(open); }

private:
	void draw_component(entt::entity entity, const wiwa::ComponentInfo& info);
	// true if the user changed the value (and the new one is in value)
	bool draw_joint_picker(entt::entity entity, const std::string& current, wiwa::FieldValue& value);
	void draw_particle_controls(entt::entity entity);
	bool draw_field(entt::entity entity, const wiwa::ComponentInfo& info, size_t index, const wiwa::FieldValue& current, wiwa::FieldValue& value);
	// Three Bool fields named <Prefix>X, <Prefix>Y, <Prefix>Z starting at index: drawn on one line like a vector.
	bool is_axis_trio(const wiwa::ComponentInfo& info, size_t index) const;
	void draw_axis_trio(entt::entity entity, const wiwa::ComponentInfo& info, size_t index);
	AssetFieldContext asset_context(wiwa::AssetType expected) const;
	// A drag or typing in a widget is one undo step: the scope opens when the widget is activated and closes when it is released.
	void track_edit_scope();
	void reset_field(entt::entity entity, const wiwa::ComponentInfo& info, size_t index);

	wiwa::SceneEditor& m_Editor;
	std::function<void()> m_Deferred{};
	std::function<void(const wiwa::AssetRef&)> m_Locate{};

	wiwa::AssetManager* m_AssetManager = nullptr;
	wiwa::AssetDatabase& m_Database;
	std::function<void(const wiwa::AssetRef&)> m_FindReferences{};
	std::function<void(const wiwa::AssetRef&)> m_OpenInAssetEditor{};

	bool m_ScopeOpen = false;
	wiwa::EntityGuid m_Pinned{};   // the entity the lock holds (not set: follow the selection)
	std::string m_AddFilter{};
	char m_AddFilterBuffer[64] = "";
	struct ComponentClipboard
	{
		std::string Component{};
		std::vector<wiwa::FieldValue> Values{};
	};
	std::optional<ComponentClipboard> m_ComponentClipboard{};

	// A quaternion is edited as Euler angles (degrees); this keeps the angles the user typed while the quaternion they make is the
	// one on the entity, so that they do not jump around.
	std::string m_EulerKey{};
	glm::vec3 m_Euler{ 0.0f };
	glm::quat m_EulerQuat{ 1.0f, 0.0f, 0.0f, 0.0f };
};
