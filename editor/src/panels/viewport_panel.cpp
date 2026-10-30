#include "viewport_panel.h"

#include <algorithm>
#include <optional>

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <gizmo/viewport_selection.h>
#include <renderer/render_module.h>
#include <scene/components.h>

using wiwa::LogLevel; // for WI_LOG

namespace
{
	constexpr float TRANSLATE_SNAP = 0.5f;      // world units, with Ctrl
	constexpr float ROTATE_SNAP_DEGREES = 15.0f;
	constexpr float SCALE_SNAP = 0.1f;
}

namespace
{
	float key_axis(ImGuiKey positive, ImGuiKey negative)
	{
		return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
	}
}

ViewportPanel::ViewportPanel(wiwa::SceneEditor& editor)
	: Panel("Viewport", PanelCategory::Window), m_Editor(editor), m_SceneGizmos(*wiwa::Engine::get().get_module<wiwa::AssetManager>()), m_GizmoSettings(wiwa::GizmoSettings::load())
{
}

// ImGuizmo hands back the entity's new world matrix; the operation in use decides which field of the local Transform it becomes.
void ViewportPanel::apply_manipulation(entt::entity entity, const glm::mat4& world)
{
	entt::registry& registry = m_Editor.scene().registry();
	if (!registry.all_of<wiwa::Transform, wiwa::Hierarchy>(entity))
	{
		return;
	}

	glm::mat4 parentWorld(1.0f);
	const entt::entity parent = registry.get<wiwa::Hierarchy>(entity).Parent;
	if (parent != entt::null && registry.valid(parent) && registry.all_of<wiwa::WorldTransform>(parent))
	{
		parentWorld = registry.get<wiwa::WorldTransform>(parent).Matrix;
	}

	const glm::mat4 local = glm::inverse(parentWorld) * world;
	float position[3], rotation[3], scale[3];
	ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), position, rotation, scale);

	if (m_Operation == ImGuizmo::TRANSLATE)
	{
		m_Editor.set_field(entity, "Transform", "Position", wiwa::FieldValue(glm::make_vec3(position)));
	}
	else if (m_Operation == ImGuizmo::ROTATE)
	{
		m_Editor.set_field(entity, "Transform", "Rotation", wiwa::FieldValue(glm::quat(glm::radians(glm::make_vec3(rotation)))));
	}
	else
	{
		m_Editor.set_field(entity, "Transform", "Scale", wiwa::FieldValue(glm::make_vec3(scale)));
	}
}

void ViewportPanel::on_draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	render->set_scene_in_viewport(true); // the scene goes to this panel, not over the window
	ImGuizmo::BeginFrame();

	ImGui::SetNextWindowPos(ImVec2(376.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(600.0f, 540.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(get_name(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (open)
	{
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
		const wiwa::u32 width = static_cast<wiwa::u32>(std::max(size.x * scale, 0.0f));
		const wiwa::u32 height = static_cast<wiwa::u32>(std::max(size.y * scale, 0.0f));

		// 0 means the panel has no room (collapsed, docked away): the renderer keeps what it has.
		render->set_viewport_size(width, height);

		if (width > 0 && height > 0)
		{
			ImGui::Image(static_cast<ImTextureID>(render->get_viewport_texture_id()), size);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("WI_ASSET"))
				{
					const std::string path = reinterpret_cast<const char*>(payload->Data);
					std::filesystem::path assetPath = path;
					if (assetPath.extension() == ".wscene")
					{
						m_Editor.open(path);
					}
				}
				ImGui::EndDragDropTarget();
			}
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 imageMin = ImGui::GetItemRectMin();

			if (m_UseEditorCamera)
			{
				const ImGuiIO& io = ImGui::GetIO();
				const bool looking = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);

				wiwa::EditorCameraInput input;
				if (looking)
				{
					input.Look = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
					input.Move = glm::vec3(key_axis(ImGuiKey_D, ImGuiKey_A), key_axis(ImGuiKey_E, ImGuiKey_Q), key_axis(ImGuiKey_W, ImGuiKey_S));
					input.Fast = io.KeyShift;
				}
				if (hovered)
				{
					input.Scroll = io.MouseWheel;
				}
				m_Camera.apply(io.DeltaTime, input);
				render->set_camera_override(m_Camera.get(size.x / std::max(size.y, 1.0f)));
			}
			else
			{
				render->set_camera_override(std::nullopt);
			}

			// Over the image, in its corner.
			ImGui::SetCursorPos(ImVec2(8.0f, 26.0f));
			ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
			if (ImGui::Button(ICON_FK_MOUSE_POINTER)) { m_Operation = -1; }
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_ARROWS)) { m_Operation = ImGuizmo::TRANSLATE; }
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_REPEAT)) { m_Operation = ImGuizmo::ROTATE; }
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_EXPAND)) { m_Operation = ImGuizmo::SCALE; }
			ImGui::SameLine();
			ImGui::Checkbox("Local", &m_LocalSpace);
			ImGui::SameLine();
			
			ImGui::Checkbox("Editor camera", &m_UseEditorCamera);
			ImGui::SameLine();
			if (ImGui::SmallButton("Gizmos"))
			{
				ImGui::OpenPopup("##gizmo_settings");
			}
			ImGui::PopStyleColor();
			if (ImGui::BeginPopup("##gizmo_settings"))
			{
				wiwa::GizmoSettings& settings = m_GizmoSettings;
				bool changed = false;
				changed |= ImGui::Checkbox("Transform handles of the selection", &settings.TransformHandles);
				changed |= ImGui::Checkbox("Grid", &settings.Grid);
				changed |= ImGui::Checkbox("World axes", &settings.Axes);
				changed |= ImGui::Checkbox("Lights", &settings.Lights);
				changed |= ImGui::Checkbox("Cameras", &settings.Cameras);
				changed |= ImGui::Checkbox("Bounds of the selection", &settings.Bounds);
				ImGui::Separator();
				changed |= ImGui::Checkbox("Outline the selection", &settings.Outline.Enabled);
				changed |= ImGui::ColorEdit3("Outline color", &settings.Outline.Color.x, ImGuiColorEditFlags_NoInputs);
				changed |= ImGui::SliderFloat("Outline width", &settings.Outline.WidthPx, 1.0f, 8.0f, "%.0f px");
				changed |= ImGui::SliderFloat("Hidden part", &settings.Outline.OccludedAlpha, 0.0f, 1.0f, "%.2f");
				m_GizmoSettingsChanged |= changed;
				ImGui::EndPopup();
			}
			if (m_GizmoSettingsChanged && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_GizmoSettings.save();
				m_GizmoSettingsChanged = false;
			}
			render->set_outline_style(m_GizmoSettings.Outline);
			if (!render->get_snapshot().HasCamera)
			{
				ImGui::SetCursorPos(ImVec2(8.0f, 50.0f));
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "No camera");
			}

			// Selection: what the renderer outlines, and the answer to the last click.
			render->set_selection(wiwa::selected_guid(m_Editor));
			if (const std::optional<wiwa::PickResult> result = render->take_pick_result())
			{
				WI_LOG(Info, "Viewport", "the renderer answered request {0}: {1}", result->RequestId, result->Guid ? result->Guid->to_string() : std::string("nothing there"));
				wiwa::apply_pick(m_Editor, *result);
			}

			// The mouse belongs to the image when it is over it, not over a widget drawn on it, and not flying the camera.
			const ImGuiIO& io = ImGui::GetIO();
			const bool flying = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
			const bool overImage = hovered && !flying && !ImGui::IsAnyItemHovered();

			if (overImage && !ImGui::IsAnyItemActive())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { m_Operation = ImGuizmo::TRANSLATE; }
				if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { m_Operation = ImGuizmo::ROTATE; }
				if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { m_Operation = ImGuizmo::SCALE; }
			}

			const bool clicked = overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			bool gizmoHasMouse = false;
			wiwa::SceneGizmoView sceneView; // without the editor camera the markers cannot be sized for the screen
			if (m_UseEditorCamera)
			{
				const wiwa::RenderCamera camera = m_Camera.get(size.x / std::max(size.y, 1.0f));
				sceneView.View = camera.View;
				sceneView.Projection = camera.Projection;
				sceneView.ViewportHeight = size.y;

				// The move / rotate / scale gizmo of the selected entity: ImGuizmo draws it and does the mouse math, on the entity's world matrix.
				const entt::entity selected = m_Editor.selected();
				entt::registry& registry = m_Editor.scene().registry();
				if (m_GizmoSettings.TransformHandles && registry.valid(selected) && registry.all_of<wiwa::WorldTransform>(selected))
				{
					ImGuizmo::SetOrthographic(false);
					ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
					ImGuizmo::SetRect(imageMin.x, imageMin.y, size.x, size.y);
					ImGuizmo::Enable(!flying);

					glm::mat4 model = registry.get<wiwa::WorldTransform>(selected).Matrix;
					float snap[3] = { 0.0f, 0.0f, 0.0f };
					if (io.KeyCtrl)
					{
						const float step = m_Operation == ImGuizmo::TRANSLATE ? TRANSLATE_SNAP : (m_Operation == ImGuizmo::ROTATE ? ROTATE_SNAP_DEGREES : SCALE_SNAP);
						snap[0] = snap[1] = snap[2] = step;
					}
					const bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.View), glm::value_ptr(camera.Projection), 
						static_cast<ImGuizmo::OPERATION>(m_Operation), m_LocalSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
						glm::value_ptr(model), nullptr, io.KeyCtrl ? snap : nullptr);
					if (changed)
					{
						apply_manipulation(selected, model);
					}
					gizmoHasMouse = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
				}
			}

			m_SceneGizmos.draw(render->get_debug_draw(), m_Editor.scene(), m_Editor.selected(), m_GizmoSettings, sceneView);

			if (clicked && !gizmoHasMouse)
			{
				WI_LOG(Info, "Viewport", "click at ({0:.0f}, {1:.0f}): asking the renderer what is there", io.MousePos.x - imageMin.x, io.MousePos.y - imageMin.y);
				const float x = std::max(io.MousePos.x - imageMin.x, 0.0f) * scale;
				const float y = std::max(io.MousePos.y - imageMin.y, 0.0f) * scale;
				render->request_pick(static_cast<wiwa::u32>(x), static_cast<wiwa::u32>(y));
			}
		}
	}
	ImGui::End();

	if (!open)
	{
		(void)render->take_pick_result(); // a click answered while the panel is hidden must not change the selection when it comes back
	}
}
