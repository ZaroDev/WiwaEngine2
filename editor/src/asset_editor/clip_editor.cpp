#include <editor_common.h>
#include "clip_editor.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <animation/pose.h>

#include <assets/asset_manager.h>
#include <assets/native_anim_files.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/atomic_write.h>
#include <asset_pipeline/file_utils.h>

#include "asset_preview.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	bool ClipEditor::load(const std::filesystem::path& file, std::string& error)
	{
		const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read " + file.string();
			return false;
		}
		std::optional<AnimationClipData> clip = read_clip_file(*bytes, error);
		if (!clip)
		{
			return false;
		}
		m_File = file;
		m_Skeleton = clip->Skeleton.valid() ? m_Host.Database.redirected(clip->Skeleton) : AssetRef{};
		m_Model = std::make_unique<ClipEditModel>(std::move(*clip));
		m_Ref = {};
		if (AssetManager* assets = Engine::get().get_module<AssetManager>())
		{
			m_Ref = assets->find_asset(file.string()).value_or(AssetRef{});
		}
		m_Selection.clear();
		m_Dope = DopeSheet{};
		m_SkeletonHandle = {};
		if (m_Skeleton.valid())
		{
			if (AssetManager* assets = Engine::get().get_module<AssetManager>())
			{
				m_SkeletonHandle = assets->load<SkeletonData>(m_Skeleton);
			}
		}
		m_Message.clear();
		return true;
	}

	bool ClipEditor::save(std::string& error)
	{
		error.clear();
		if (!m_Model)
		{
			error = "there is nothing open";
			return false;
		}
		const std::vector<std::byte> bytes = write_clip_file(m_Model->data());
		if (!write_file_atomic(m_File, bytes.data(), bytes.size(), error))
		{
			return false;
		}
		m_Model->mark_saved();
		return true;
	}

	void ClipEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		const AnimationClipData& clip = m_Model->data();
		size_t keys = 0;
		for (const AnimationChannel& channel : clip.Channels)
		{
			keys += channel.Times.size();
		}
		draw_clip_menu();
		ImGui::Text("%s", clip.Name.c_str());
		ImGui::TextDisabled("%.2f s, %zu channels, %zu keys, %zu events", clip.Duration, clip.Channels.size(), keys, clip.Events.size());
		ImGui::Checkbox("Snap to frames", &m_Snap);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		ImGui::DragFloat("fps", &m_SnapFps, 0.5f, 1.0f, 240.0f, "%.0f");
		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			m_Dope.fit();
		}
		ImGui::TextDisabled("%zu keys selected", m_Selection.size());
		const bool canKey = m_SkeletonHandle.get() != nullptr && !m_Dope.selected_joint().empty();
		ImGui::BeginDisabled(!canKey);
		if (ImGui::Button("Key pose (K)"))
		{
			key_pending_pose();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", canKey ? "writes the gizmo's pose for the selected joint at the playhead" : "select a joint in the dope sheet");
		draw_dialog();
		ImGui::TextDisabled("Click selects (Shift/Ctrl adds), drag moves, Alt-drag scales about the playhead, Ctrl+wheel zooms, middle button pans");
		if (!m_Model->error().empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_Model->error().c_str());
		}
		if (!m_Message.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_Message.c_str());
		}
	}

	void ClipEditor::draw_clip_menu()
	{
		if (ImGui::Button("Clip..."))
		{
			ImGui::OpenPopup("clipmenu##clip");
		}
		if (ImGui::BeginPopup("clipmenu##clip"))
		{
			const AnimationClipData& clip = m_Model->data();
			const auto open = [&](Dialog dialog, float a, float b, float c)
			{
				m_Dialog = dialog;
				m_OpenDialog = true;
				m_A = a;
				m_B = b;
				m_C = c;
				m_Model->clear_error();
				std::snprintf(m_Text, sizeof(m_Text), "%s", clip.Name.c_str());
			};
			if (ImGui::MenuItem("Trim...")) { open(Dialog::Trim, 0.0f, clip.Duration, 0.0f); }
			if (ImGui::MenuItem("Scale time...")) { open(Dialog::ScaleTime, 1.0f, 0.0f, 0.0f); }
			if (ImGui::MenuItem("Set duration...")) { open(Dialog::Duration, clip.Duration, 0.0f, 0.0f); }
			if (ImGui::MenuItem("Reverse")) { m_Selection.clear(); m_Model->reverse(); }
			if (ImGui::MenuItem("Resample...")) { open(Dialog::Resample, m_SnapFps, 0.0f, 0.0f); }
			if (ImGui::MenuItem("Reduce keys...")) { open(Dialog::Reduce, 0.001f, 0.1f, 0.001f); }
			if (ImGui::MenuItem("Make additive...")) { open(Dialog::Additive, 0.0f, 0.0f, 0.0f); }
			if (ImGui::MenuItem("Rename...")) { open(Dialog::Rename, 0.0f, 0.0f, 0.0f); }
			ImGui::EndPopup();
		}
	}

	void ClipEditor::draw_dialog()
	{
		if (m_OpenDialog)
		{
			ImGui::OpenPopup("Clip operation##clip");
			m_OpenDialog = false;
		}
		if (!ImGui::BeginPopupModal("Clip operation##clip", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		bool apply = false;
		bool succeeded = false;
		const float duration = m_Model->data().Duration;
		switch (m_Dialog)
		{
		case Dialog::Trim:
			ImGui::TextUnformatted("Keep the part of the clip between two times; it starts at 0 afterwards.");
			ImGui::DragFloat("From (s)", &m_A, 0.01f, 0.0f, duration, "%.3f");
			ImGui::DragFloat("To (s)", &m_B, 0.01f, 0.0f, duration, "%.3f");
			apply = ImGui::Button("Trim");
			if (apply) { succeeded = m_Model->trim(m_A, m_B); }
			break;
		case Dialog::ScaleTime:
			ImGui::TextUnformatted("Multiply every time (keys, events, duration) by a factor.");
			ImGui::DragFloat("Factor", &m_A, 0.01f, 0.01f, 100.0f, "%.3f");
			apply = ImGui::Button("Scale");
			if (apply) { succeeded = m_Model->scale_time(m_A); }
			break;
		case Dialog::Duration:
			ImGui::TextUnformatted("Set the length; shorter drops the keys past it, longer keeps the last pose.");
			ImGui::DragFloat("Seconds", &m_A, 0.01f, 0.01f, 3600.0f, "%.3f");
			apply = ImGui::Button("Set");
			if (apply) { succeeded = m_Model->set_duration(m_A); }
			break;
		case Dialog::Resample:
			ImGui::TextUnformatted("Sample the clip at a rate and write linear keys.");
			ImGui::DragFloat("Frames per second", &m_A, 0.5f, 1.0f, 240.0f, "%.0f");
			apply = ImGui::Button("Resample");
			if (apply) { succeeded = m_Model->resample(m_A); }
			break;
		case Dialog::Reduce:
			ImGui::TextUnformatted("Drop keys that the neighbours reproduce within the tolerances.");
			ImGui::DragFloat("Translation", &m_A, 0.0005f, 0.0f, 1.0f, "%.4f");
			ImGui::DragFloat("Rotation (degrees)", &m_B, 0.01f, 0.0f, 45.0f, "%.3f");
			ImGui::DragFloat("Scale", &m_C, 0.0005f, 0.0f, 1.0f, "%.4f");
			apply = ImGui::Button("Reduce");
			if (apply)
			{
				m_Model->clear_error();
				const size_t dropped = m_Model->reduce_keys(m_A, m_B, m_C);
				succeeded = m_Model->error().empty();
				m_Message = succeeded ? std::to_string(dropped) + " keys dropped" : std::string{};
			}
			break;
		case Dialog::Additive:
			ImGui::TextUnformatted("Make every key relative to the pose at a time (the clip becomes additive).");
			ImGui::DragFloat("Reference time (s)", &m_A, 0.01f, 0.0f, duration, "%.3f");
			apply = ImGui::Button("Make additive");
			if (apply) { succeeded = m_Model->make_additive(m_A); }
			break;
		case Dialog::Rename:
			ImGui::InputText("Name", m_Text, sizeof(m_Text));
			apply = ImGui::Button("Rename");
			if (apply) { succeeded = m_Model->rename(m_Text); }
			break;
		case Dialog::None:
			break;
		}
		ImGui::SameLine();
		const bool cancelled = ImGui::Button("Cancel");
		if (!m_Model->error().empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_Model->error().c_str());
		}
		if (succeeded || cancelled)
		{
			if (succeeded)
			{
				m_Selection.clear();
			}
			m_Model->clear_error();
			m_Dialog = Dialog::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ClipEditor::key_pending_pose()
	{
		const std::string joint = m_Dope.selected_joint();
		if (!m_Model || joint.empty())
		{
			return;
		}
		if (!m_HasPending || m_PendingJoint != joint)
		{
			// nothing was moved with the gizmo: key the pose the preview shows
			const SkeletonData* skeleton = m_SkeletonHandle.get();
			const int index = m_Host.Preview.joint_index(joint);
			if (skeleton == nullptr || index < 0 || !m_Host.Preview.has_pose() || static_cast<size_t>(index) >= skeleton->Parents.size())
			{
				return;
			}
			const size_t j = static_cast<size_t>(index);
			const glm::mat4 parent = skeleton->Parents[j] >= 0 ? m_Host.Preview.joint_matrix(static_cast<size_t>(skeleton->Parents[j])) : glm::mat4(1.0f);
			const glm::mat4 pre = j < skeleton->Pre.size() ? skeleton->Pre[j] : glm::mat4(1.0f);
			decompose_matrix(glm::inverse(parent * pre) * m_Host.Preview.joint_matrix(j), m_PendingT, m_PendingR, m_PendingS);
			m_PendingJoint = joint;
		}
		const float time = std::clamp(m_Host.Preview.clip_time(), 0.0f, m_Model->data().Duration);
		m_Model->key_pose(joint, time, m_PendingT, m_PendingR, m_PendingS, m_SkeletonHandle.get());
		m_HasPending = false;
	}

	void ClipEditor::draw_gizmo()
	{
		const SkeletonData* skeleton = m_SkeletonHandle.get();
		const std::string joint = m_Model ? m_Dope.selected_joint() : std::string{};
		const int index = joint.empty() ? -1 : m_Host.Preview.joint_index(joint);
		if (skeleton == nullptr || index < 0 || !m_Host.Preview.has_pose() || static_cast<size_t>(index) >= skeleton->Parents.size())
		{
			m_HasPending = false;
			return;
		}
		const size_t j = static_cast<size_t>(index);
		const float time = m_Host.Preview.clip_time();
		const glm::mat4 parent = skeleton->Parents[j] >= 0 ? m_Host.Preview.joint_matrix(static_cast<size_t>(skeleton->Parents[j])) : glm::mat4(1.0f);
		const glm::mat4 pre = j < skeleton->Pre.size() ? skeleton->Pre[j] : glm::mat4(1.0f);
		const glm::mat4 base = parent * pre;
		// a pending pose belongs to a joint and a time; moving either drops it
		if (m_HasPending && (m_PendingJoint != joint || std::abs(m_PendingTime - time) > 1e-4f))
		{
			m_HasPending = false;
		}
		glm::mat4 world = m_HasPending ? base * glm::translate(glm::mat4(1.0f), m_PendingT) * glm::mat4_cast(m_PendingR) * glm::scale(glm::mat4(1.0f), m_PendingS)
			: m_Host.Preview.joint_matrix(j);

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { m_GizmoOperation = 0; }
			if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { m_GizmoOperation = 1; }
			if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { m_GizmoOperation = 2; }
			if (ImGui::IsKeyPressed(ImGuiKey_K, false))
			{
				key_pending_pose();
				return;
			}
		}
		const ImGuizmo::OPERATION operation = m_GizmoOperation == 0 ? ImGuizmo::TRANSLATE : m_GizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;

		ImGuizmo::BeginFrame();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(m_Host.Preview.image_min().x, m_Host.Preview.image_min().y, m_Host.Preview.image_size().x, m_Host.Preview.image_size().y);
		const RenderCamera& camera = m_Host.Preview.camera();
		const bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.View), glm::value_ptr(camera.Projection), operation, ImGuizmo::LOCAL, glm::value_ptr(world));
		if (changed)
		{
			decompose_matrix(glm::inverse(base) * world, m_PendingT, m_PendingR, m_PendingS);
			m_PendingJoint = joint;
			m_PendingTime = time;
			m_HasPending = true;
		}
	}

	int ClipEditor::selected_joint() const
	{
		return m_Dope.selected_joint().empty() ? -1 : m_Host.Preview.joint_index(m_Dope.selected_joint());
	}

	void ClipEditor::draw_bottom(ImVec2 size)
	{
		if (!m_Model)
		{
			return;
		}
		DopeSheetOptions options;
		options.Snap = m_Snap;
		options.SnapFps = m_SnapFps;
		options.Skeleton = m_SkeletonHandle.get();
		float playhead = std::clamp(m_Host.Preview.clip_time(), 0.0f, m_Model->data().Duration);
		const float dopeHeight = std::max(size.y * 0.55f, 120.0f);
		if (m_Dope.draw(*m_Model, playhead, m_Selection, options, ImVec2(size.x, dopeHeight)))
		{
			m_Host.Preview.set_playing(false);
			m_Host.Preview.seek(playhead);
		}
		const float curvesHeight = ImGui::GetContentRegionAvail().y;
		if (curvesHeight > 60.0f)
		{
			m_Curves.draw(*m_Model, m_Dope.selected_joint(), m_Selection, m_SnapFps, ImVec2(size.x, curvesHeight));
		}
	}
}
