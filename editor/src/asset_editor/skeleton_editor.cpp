#include <editor_common.h>
#include "skeleton_editor.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <animation/pose.h>
#include <assets/asset_manager.h>
#include <assets/native_anim_files.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/file_utils.h>

#include "asset_preview.h"
#include "skeleton_index.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		std::string read_text(const std::filesystem::path& file, bool& ok)
		{
			const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
			ok = bytes.has_value();
			return ok ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string{};
		}

		ImVec4 error_color() { return ImVec4(1.0f, 0.45f, 0.4f, 1.0f); }
	}

	bool SkeletonEditor::load(const std::filesystem::path& file, std::string& error)
	{
		bool readable = false;
		const std::string text = read_text(file, readable);
		if (!readable)
		{
			error = "cannot read " + file.string();
			return false;
		}
		std::optional<SkeletonData> skeleton = read_skeleton_file(text, error);
		if (!skeleton)
		{
			return false;
		}
		m_File = file;
		m_Model = std::make_unique<SkeletonEditModel>(std::move(*skeleton));
		m_Ref = {};
		if (AssetManager* assets = Engine::get().get_module<AssetManager>())
		{
			if (const std::optional<AssetRef> ref = assets->find_asset(file.string()))
			{
				m_Ref = *ref;
			}
		}
		m_SelectedJoint = m_Model->data().JointNames.empty() ? -1 : 0;
		m_SelectedSocket = -1;
		m_RenamingJoint = -1;
		m_Message.clear();
		return true;
	}

	void SkeletonEditor::clamp_selection()
	{
		if (!m_Model)
		{
			return;
		}
		if (m_SelectedSocket >= static_cast<int>(m_Model->data().Sockets.size()))
		{
			m_SelectedSocket = -1;
		}
		m_RenamingJoint = -1;
	}

	bool SkeletonEditor::save(std::string& error)
	{
		error.clear();
		if (!m_Model)
		{
			error = "there is nothing open";
			return false;
		}
		m_PlannedRenames = m_Model->renames();
		m_Dependents = SkeletonDependents{};
		if (!m_PlannedRenames.empty() && !m_Ref.valid())
		{
			if (AssetManager* assets = Engine::get().get_module<AssetManager>())
			{
				m_Ref = assets->find_asset(m_File.string()).value_or(AssetRef{});
			}
			if (!m_Ref.valid())
			{
				// the meshes and clips that name these joints cannot be found: renaming now would leave them pointing at nothing
				error = "The asset catalog does not know this file yet, so the files that use its joints cannot be updated. Try again in a moment.";
				return false;
			}
		}
		if (!m_PlannedRenames.empty())
		{
			m_Dependents = find_skeleton_dependents(m_Host.Database, m_Ref.Guid, m_PlannedRenames);
			const bool touches = std::any_of(m_Dependents.Files.begin(), m_Dependents.Files.end(), [](const SkeletonDependent& d) { return d.Joints > 0; });
			if (touches || !m_Dependents.Unreadable.empty())
			{
				m_ConfirmRequested = true; // a popup asks first (drawn by draw_tools)
				m_ConfirmError.clear();
				return false;
			}
		}
		return write_now(error);
	}

	bool SkeletonEditor::write_now(std::string& error)
	{
		const std::string text = write_skeleton_file(m_Model->data());
		if (!apply_joint_renames(m_Dependents, m_PlannedRenames, m_File, text, error))
		{
			return false;
		}
		m_Model->mark_saved();
		m_Dependents = SkeletonDependents{};
		m_PlannedRenames.clear();
		return true;
	}

	void SkeletonEditor::draw_confirm()
	{
		if (m_ConfirmRequested)
		{
			ImGui::OpenPopup("Rename joints##skeleton");
			m_ConfirmRequested = false;
			m_ConfirmOpen = true;
		}
		if (!ImGui::BeginPopupModal("Rename joints##skeleton", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		ImGui::TextUnformatted("These joints are renamed:");
		for (const auto& [from, to] : m_PlannedRenames)
		{
			ImGui::BulletText("%s  ->  %s", from.c_str(), to.c_str());
		}
		ImGui::Separator();
		bool blocked = false;
		if (std::none_of(m_Dependents.Files.begin(), m_Dependents.Files.end(), [](const SkeletonDependent& d) { return d.Joints > 0; }))
		{
			ImGui::TextDisabled("No mesh or clip names them.");
		}
		else
		{
			ImGui::TextUnformatted("These files name them and are rewritten with the skeleton:");
			for (const SkeletonDependent& item : m_Dependents.Files)
			{
				if (item.Joints == 0)
				{
					continue;
				}
				ImGui::BulletText("%s  (%u joint%s)%s", item.File.filename().string().c_str(), item.Joints, item.Joints == 1 ? "" : "s", item.Writable ? "" : "  READ-ONLY");
				blocked = blocked || !item.Writable;
			}
		}
		if (!m_Dependents.Unreadable.empty())
		{
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "These files could not be read and are not checked:");
			for (const std::filesystem::path& file : m_Dependents.Unreadable)
			{
				ImGui::BulletText("%s", file.filename().string().c_str());
			}
		}
		if (blocked)
		{
			ImGui::TextColored(error_color(), "A read-only file cannot be rewritten: make it writable first.");
		}
		if (!m_ConfirmError.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_ConfirmError.c_str());
		}
		ImGui::Separator();
		ImGui::BeginDisabled(blocked);
		if (ImGui::Button("Apply"))
		{
			// the files may have changed since the list was made: it is made again
			m_Dependents = find_skeleton_dependents(m_Host.Database, m_Ref.Guid, m_PlannedRenames);
			if (write_now(m_ConfirmError))
			{
				m_ConfirmOpen = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_ConfirmOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void SkeletonEditor::draw_joint(size_t joint, const std::vector<std::vector<size_t>>& children)
	{
		const SkeletonData& skeleton = m_Model->data();
		ImGui::PushID(static_cast<int>(joint));
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (children[joint].empty())
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (static_cast<int>(joint) == m_SelectedJoint)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		bool open = false;
		if (m_RenamingJoint == static_cast<int>(joint))
		{
			open = ImGui::TreeNodeEx("##rename", flags, "");
			ImGui::SameLine();
			if (m_RenameFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}
			ImGui::SetNextItemWidth(-1.0f);
			const bool commit = ImGui::InputText("##name", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if (commit)
			{
				std::string why;
				if (m_Model->rename_joint(joint, m_RenameBuffer, why))
				{
					m_Message.clear();
				}
				else
				{
					m_Message = why;
				}
				m_RenamingJoint = -1;
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && !ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
			{
				m_RenamingJoint = -1;
			}
		}
		else
		{
			open = ImGui::TreeNodeEx(skeleton.JointNames[joint].c_str(), flags);
			if (ImGui::IsItemClicked())
			{
				m_SelectedJoint = static_cast<int>(joint);
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				m_RenamingJoint = static_cast<int>(joint);
				m_RenameFocus = true;
				std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", skeleton.JointNames[joint].c_str());
			}
		}
		if (open)
		{
			for (size_t child : children[joint])
			{
				draw_joint(child, children);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SkeletonEditor::draw_socket_details()
	{
		const SkeletonData& skeleton = m_Model->data();
		if (m_SelectedSocket < 0 || static_cast<size_t>(m_SelectedSocket) >= skeleton.Sockets.size())
		{
			return;
		}
		const size_t index = static_cast<size_t>(m_SelectedSocket);
		const SkeletonSocket socket = skeleton.Sockets[index];

		char name[SkeletonEditModel::MAX_NAME_LENGTH + 1];
		std::snprintf(name, sizeof(name), "%s", socket.Name.c_str());
		ImGui::SetNextItemWidth(-60.0f);
		if (ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			std::string why;
			m_Message = m_Model->rename_socket(index, name, why) ? std::string{} : why;
		}

		ImGui::SetNextItemWidth(-60.0f);
		if (ImGui::BeginCombo("Joint", socket.Joint.c_str()))
		{
			for (const std::string& joint : skeleton.JointNames)
			{
				if (ImGui::Selectable(joint.c_str(), joint == socket.Joint))
				{
					m_Model->set_socket_joint(index, joint);
				}
			}
			ImGui::EndCombo();
		}

		glm::vec3 position = socket.Position;
		glm::vec3 euler = glm::degrees(glm::eulerAngles(socket.Rotation));
		glm::vec3 scale = socket.Scale;
		bool changed = false;
		ImGui::SetNextItemWidth(-60.0f);
		changed |= ImGui::DragFloat3("Position", &position.x, 0.005f);
		if (ImGui::IsItemActivated()) { m_Model->begin_gesture(); }
		if (ImGui::IsItemDeactivated()) { m_Model->end_gesture(); }
		ImGui::SetNextItemWidth(-60.0f);
		changed |= ImGui::DragFloat3("Rotation", &euler.x, 0.5f);
		if (ImGui::IsItemActivated()) { m_Model->begin_gesture(); }
		if (ImGui::IsItemDeactivated()) { m_Model->end_gesture(); }
		ImGui::SetNextItemWidth(-60.0f);
		changed |= ImGui::DragFloat3("Scale", &scale.x, 0.005f);
		if (ImGui::IsItemActivated()) { m_Model->begin_gesture(); }
		if (ImGui::IsItemDeactivated()) { m_Model->end_gesture(); }
		if (changed)
		{
			m_Model->set_socket_transform(index, position, glm::quat(glm::radians(euler)), scale);
		}

		if (ImGui::Button("Delete socket"))
		{
			m_Model->remove_socket(index);
			m_SelectedSocket = -1;
		}
	}

	void SkeletonEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		const SkeletonData& skeleton = m_Model->data();
		const SkeletonUsers users = m_Ref.valid() ? m_Host.Index.users_of(m_Ref.Guid) : SkeletonUsers{};
		ImGui::Text("%zu joints, %zu sockets", skeleton.JointNames.size(), skeleton.Sockets.size());
		if (m_Host.Index.ready())
		{
			ImGui::TextDisabled("Used by %zu skinned meshes and %zu clips", users.Meshes.size(), users.Clips.size());
		}
		if (!m_Message.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Message.c_str());
		}

		if (ImGui::CollapsingHeader("Joints", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Double-click or F2 to rename");
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_SelectedJoint >= 0 && m_RenamingJoint < 0)
			{
				m_RenamingJoint = m_SelectedJoint;
				m_RenameFocus = true;
				std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", skeleton.JointNames[static_cast<size_t>(m_SelectedJoint)].c_str());
			}
			std::vector<std::vector<size_t>> children(skeleton.JointNames.size());
			std::vector<size_t> roots;
			for (size_t i = 0; i < skeleton.Parents.size() && i < children.size(); i++)
			{
				if (skeleton.Parents[i] >= 0)
				{
					children[static_cast<size_t>(skeleton.Parents[i])].push_back(i);
				}
				else
				{
					roots.push_back(i);
				}
			}
			ImGui::BeginChild("##joints", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
			for (size_t root : roots)
			{
				draw_joint(root, children);
			}
			ImGui::EndChild();
		}

		if (ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const bool canAdd = m_SelectedJoint >= 0 && static_cast<size_t>(m_SelectedJoint) < skeleton.JointNames.size();
			ImGui::BeginDisabled(!canAdd);
			if (ImGui::Button("Add socket on the selected joint"))
			{
				const size_t added = m_Model->add_socket(skeleton.JointNames[static_cast<size_t>(m_SelectedJoint)]);
				if (added != SIZE_MAX)
				{
					m_SelectedSocket = static_cast<int>(added);
				}
			}
			ImGui::EndDisabled();
			for (size_t i = 0; i < skeleton.Sockets.size(); i++)
			{
				const std::string label = skeleton.Sockets[i].Name + "  (" + skeleton.Sockets[i].Joint + ")##socket" + std::to_string(i);
				if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == m_SelectedSocket))
				{
					m_SelectedSocket = static_cast<int>(i);
				}
			}
			draw_socket_details();
		}
		draw_confirm();
	}

	void SkeletonEditor::draw_gizmo()
	{
		static int operation = ImGuizmo::TRANSLATE;
		if (!m_Model || m_SelectedSocket < 0 || static_cast<size_t>(m_SelectedSocket) >= m_Model->data().Sockets.size() || !m_Host.Preview.has_pose())
		{
			if (m_GizmoGesture)
			{
				m_Model->end_gesture();
				m_GizmoGesture = false;
			}
			return;
		}
		const SkeletonData& skeleton = m_Model->data();
		const SkeletonSocket socket = skeleton.Sockets[static_cast<size_t>(m_SelectedSocket)];
		const auto joint = std::find(skeleton.JointNames.begin(), skeleton.JointNames.end(), socket.Joint);
		if (joint == skeleton.JointNames.end())
		{
			return;
		}
		const glm::mat4 jointMatrix = m_Host.Preview.joint_matrix(static_cast<size_t>(joint - skeleton.JointNames.begin()));
		glm::mat4 world = jointMatrix * glm::translate(glm::mat4(1.0f), socket.Position) * glm::mat4_cast(socket.Rotation) * glm::scale(glm::mat4(1.0f), socket.Scale);

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { operation = ImGuizmo::TRANSLATE; }
			if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { operation = ImGuizmo::ROTATE; }
			if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { operation = ImGuizmo::SCALE; }
		}

		ImGuizmo::BeginFrame();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(m_Host.Preview.image_min().x, m_Host.Preview.image_min().y, m_Host.Preview.image_size().x, m_Host.Preview.image_size().y);
		const RenderCamera& camera = m_Host.Preview.camera();
		const bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.View), glm::value_ptr(camera.Projection), static_cast<ImGuizmo::OPERATION>(operation),
			ImGuizmo::LOCAL, glm::value_ptr(world));

		if (ImGuizmo::IsUsing() && !m_GizmoGesture)
		{
			m_Model->begin_gesture();
			m_GizmoGesture = true;
		}
		if (changed)
		{
			const glm::mat4 local = glm::inverse(jointMatrix) * world;
			glm::vec3 position;
			glm::quat rotation;
			glm::vec3 scale;
			decompose_matrix(local, position, rotation, scale);
			m_Model->set_socket_transform(static_cast<size_t>(m_SelectedSocket), position, rotation, scale);
		}
		if (!ImGuizmo::IsUsing() && m_GizmoGesture)
		{
			m_Model->end_gesture();
			m_GizmoGesture = false;
		}
	}
}
