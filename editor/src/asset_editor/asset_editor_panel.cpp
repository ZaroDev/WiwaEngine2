#include <editor_common.h>
#include "asset_editor_panel.h"

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>

#include "asset_kinds.h"
#include "clip_editor.h"
#include "../ai/bt_editor.h"
#include "graph_editor.h"
#include "vfx_editor.h"
#include "mesh_editor.h"
#include "preview_mesh_resolver.h"
#include "skeleton_editor.h"
#include "vehicle_editor.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace
{
	ImVec4 error_color() { return ImVec4(1.0f, 0.45f, 0.4f, 1.0f); }
}

AssetEditorPanel::AssetEditorPanel(wiwa::AssetDatabase& database, wiwa::EditorState& state, wiwa::SkeletonIndex& index)
	: Panel("Asset Editor", PanelCategory::Window), m_Database(database), m_State(state), m_Index(index)
{
	m_Open = false; // opened from the Asset Browser
}

AssetEditorPanel::~AssetEditorPanel()
{
	m_Editor.reset();
}

bool AssetEditorPanel::can_open(const std::filesystem::path& file)
{
	return wiwa::opens_in_asset_editor(file);
}

bool AssetEditorPanel::open_asset(const wiwa::AssetRef& ref)
{
	if (const std::optional<std::filesystem::path> path = m_Database.path_of(m_Database.redirected(ref).Guid))
	{
		return open_file(*path);
	}
	return false;
}

bool AssetEditorPanel::open_file(const std::filesystem::path& file)
{
	if (!can_open(file))
	{
		return false;
	}
	m_Open = true;
	m_FocusRequested = true;
	if (m_Editor != nullptr && m_Editor->dirty() && m_Editor->file() != file)
	{
		m_Pending = Pending::Open;
		m_PendingFile = file;
		m_PromptRequested = true;
		return true;
	}
	load_now(file);
	return true;
}

bool AssetEditorPanel::load_now(const std::filesystem::path& file)
{
	std::unique_ptr<wiwa::IAssetEditor> editor;
	const wiwa::AssetEditorHost host{ m_Database, m_State, m_Index, m_Preview };
	const wiwa::AssetKindInfo* kind = wiwa::find_asset_kind(file);
	switch (kind != nullptr ? kind->Editor : wiwa::AssetEditorKind::None)
	{
	case wiwa::AssetEditorKind::Skeleton: editor = std::make_unique<wiwa::SkeletonEditor>(host); break;
	case wiwa::AssetEditorKind::Clip: editor = std::make_unique<wiwa::ClipEditor>(host); break;
	case wiwa::AssetEditorKind::Graph: editor = std::make_unique<wiwa::GraphEditor>(host); break;
	case wiwa::AssetEditorKind::Effect: editor = std::make_unique<wiwa::VfxEditor>(host); break;
	case wiwa::AssetEditorKind::BehaviorTree: editor = std::make_unique<wiwa::BehaviorTreeEditor>(host); break;
	case wiwa::AssetEditorKind::Mesh: editor = std::make_unique<wiwa::MeshEditor>(host); break;
	case wiwa::AssetEditorKind::Vehicle: editor = std::make_unique<wiwa::VehicleEditor>(host); break;
	case wiwa::AssetEditorKind::None:
		m_Error = file.filename().string() + ": no Asset Editor tool opens this kind of file";
		m_Editor.reset();
		m_HaveFileTime = false;
		return false;
	}
	std::string error;
	if (!editor->load(file, error))
	{
		m_Error = file.filename().string() + ": " + error;
		m_Editor.reset();
		m_HaveFileTime = false;
		return false;
	}
	m_Error.clear();
	m_Status.clear();
	m_Editor = std::move(editor);
	m_Clip = {};
	m_Skeleton = {};
	m_ChoiceStale = true;
	m_ExternalChange = false;
	std::error_code time;
	m_FileTime = std::filesystem::last_write_time(file, time);
	m_HaveFileTime = !time;
	return true;
}

std::string AssetEditorPanel::title() const
{
	std::string name = "Asset Editor";
	if (m_Editor != nullptr)
	{
		name += " - " + m_Editor->file().filename().string() + (m_Editor->dirty() ? "*" : "");
	}
	return name + "###AssetEditor";
}

void AssetEditorPanel::on_update()
{
	if (m_Editor != nullptr && is_active())
	{
		poll_file();
	}
}

// The file changed on disk (a re-import wrote it, a pull, another tool): reloaded at once while nothing is unsaved, else the user chooses.
void AssetEditorPanel::poll_file()
{
	if (!m_HaveFileTime || ++m_PollCounter < 30)
	{
		return;
	}
	m_PollCounter = 0;
	std::error_code time;
	const std::filesystem::file_time_type now = std::filesystem::last_write_time(m_Editor->file(), time);
	if (time || now == m_FileTime)
	{
		return;
	}
	if (m_Editor->dirty())
	{
		m_ExternalChange = true;
	}
	else
	{
		load_now(m_Editor->file());
	}
}

void AssetEditorPanel::save()
{
	if (m_Editor == nullptr)
	{
		return;
	}
	std::string error;
	if (m_Editor->save(error))
	{
		m_Error.clear();
		m_Status = "Saved";
		std::error_code time;
		m_FileTime = std::filesystem::last_write_time(m_Editor->file(), time);
		m_HaveFileTime = !time;
		m_ExternalChange = false;
	}
	else if (!error.empty())
	{
		m_Error = error;
	}
}

void AssetEditorPanel::handle_keys()
{
	if (m_Editor == nullptr || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
	{
		return;
	}
	const ImGuiIO& io = ImGui::GetIO();
	if (!io.KeyCtrl)
	{
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		save();
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift)
	{
		m_Editor->undo();
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift))
	{
		m_Editor->redo();
	}
}

void AssetEditorPanel::draw_toolbar()
{
	ImGui::BeginDisabled(!m_Editor->can_undo());
	if (ImGui::Button("Undo"))
	{
		m_Editor->undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Editor->can_redo());
	if (ImGui::Button("Redo"))
	{
		m_Editor->redo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Editor->dirty());
	if (ImGui::Button("Save"))
	{
		save();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%s  %s", m_Editor->kind(), m_Editor->file().filename().string().c_str());
	if (!m_Status.empty() && !m_Editor->dirty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("- %s", m_Status.c_str());
	}
}

// What the preview poses: the mesh the editor shows itself, else the override of the editor state for the skeleton, else the first mesh that skins
// to it. Looked up again when the index scanned or the skeleton changed.
void AssetEditorPanel::update_preview_choice()
{
	wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
	const wiwa::AssetRef skeleton = m_Editor->preview_skeleton();
	if (m_ChoiceStale || !(skeleton == m_Skeleton) || m_Index.generation() != m_IndexGeneration)
	{
		m_Skeleton = skeleton;
		m_IndexGeneration = m_Index.generation();
		m_ChoiceStale = false;
		m_Meshes.clear();
		m_Clips.clear();
		m_PreviewMesh = {};
		if (assets != nullptr && skeleton.valid())
		{
			m_Meshes = wiwa::skinned_meshes_of(*assets, m_Index, skeleton.Guid);
			m_Clips = wiwa::compatible_clips_of(*assets, m_Index, skeleton.Guid);
			m_PreviewMesh = wiwa::resolve_preview_mesh(*assets, m_Index, m_State, skeleton.Guid).value_or(wiwa::AssetRef{});
		}
		if (m_Clip.valid() && std::none_of(m_Clips.begin(), m_Clips.end(), [&](const wiwa::AssetRef& clip) { return clip == m_Clip; }))
		{
			m_Clip = {};
		}
	}
	const wiwa::AssetRef own = m_Editor->own_mesh();
	const wiwa::AssetRef ownClip = m_Editor->own_clip();
	const wiwa::AssetRef ownGraph = m_Editor->own_graph();
	if (ownClip.valid())
	{
		m_Clip = ownClip; // a clip editor plays the clip it edits
	}
	if (ownGraph.valid())
	{
		m_Clip = {}; // and a graph editor the graph
	}
	m_Preview.show(skeleton, own.valid() ? own : m_PreviewMesh, m_Clip, ownGraph);
	m_Preview.set_edit_skeleton(m_Editor->edit_skeleton());
	m_Preview.set_selection(m_Editor->selected_joint(), m_Editor->selected_socket());
}

void AssetEditorPanel::draw_preview_controls()
{
	wiwa::AssetPreview::Overlays& overlays = m_Preview.overlays();
	ImGui::Checkbox("Mesh", &overlays.Mesh);
	ImGui::SameLine();
	ImGui::Checkbox("Bones", &overlays.Bones);
	ImGui::SameLine();
	ImGui::Checkbox("Names", &overlays.JointNames);
	ImGui::SameLine();
	ImGui::Checkbox("Sockets", &overlays.Sockets);

	const auto name_of = [&](const wiwa::AssetRef& ref) -> std::string
	{
		const std::optional<std::filesystem::path> path = m_Database.path_of(ref.Guid);
		return path ? path->stem().string() : std::string("(unknown)");
	};

	// the clip to play (a clip editor plays its own)
	const bool ownGraph = m_Editor->own_graph().valid();
	const bool ownClip = m_Editor->own_clip().valid() || ownGraph;
	ImGui::BeginDisabled(ownClip);
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("Clip", m_Clip.valid() ? name_of(m_Clip).c_str() : "(bind pose)"))
	{
		if (ImGui::Selectable("(bind pose)", !m_Clip.valid()))
		{
			m_Clip = {};
		}
		for (const wiwa::AssetRef& clip : m_Clips)
		{
			if (ImGui::Selectable(name_of(clip).c_str(), clip == m_Clip))
			{
				m_Clip = clip;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	if (m_Clip.valid() || ownGraph)
	{
		ImGui::SameLine();
		if (ImGui::Button(m_Preview.playing() ? "Pause" : "Play"))
		{
			m_Preview.set_playing(!m_Preview.playing());
		}
		const float duration = m_Preview.clip_duration();
		if (duration > 0.0f)
		{
			float time = m_Preview.clip_time();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderFloat("##time", &time, 0.0f, duration, "%.2f s"))
			{
				m_Preview.set_playing(false);
				m_Preview.seek(time);
			}
		}
	}

	// the mesh a skeleton is previewed with (a mesh editor shows its own)
	if (!m_Editor->own_mesh().valid() && m_Skeleton.valid())
	{
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("Preview mesh", m_PreviewMesh.valid() ? name_of(m_PreviewMesh).c_str() : "(bones only)"))
		{
			for (const wiwa::AssetRef& mesh : m_Meshes)
			{
				if (ImGui::Selectable(name_of(mesh).c_str(), mesh == m_PreviewMesh))
				{
					m_State.set_preview_mesh(m_Skeleton.Guid, mesh.Guid);
					m_ChoiceStale = true;
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!m_Preview.notice().empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", m_Preview.notice().c_str());
	}
}

void AssetEditorPanel::draw_preview_column()
{
	if (m_Editor->draws_own_preview())
	{
		m_Preview.hide(); // the skeleton preview is not drawn: it gives its view back
		const ImVec2 room = ImGui::GetContentRegionAvail();
		m_Editor->draw_preview(ImVec2(room.x, std::max(room.y, 64.0f)));
		return;
	}
	update_preview_choice();
	draw_preview_controls();
	const ImVec2 available = ImGui::GetContentRegionAvail();
	m_Preview.draw(ImVec2(available.x, std::max(available.y, 64.0f)));
	m_Editor->draw_gizmo();
}

void AssetEditorPanel::draw_prompt()
{
	if (m_PromptRequested)
	{
		ImGui::OpenPopup("Unsaved changes##asseteditor");
		m_PromptRequested = false;
	}
	if (!ImGui::BeginPopupModal("Unsaved changes##asseteditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}
	ImGui::Text("%s has changes that are not saved.", m_Editor != nullptr ? m_Editor->file().filename().string().c_str() : "The asset");
	const auto proceed = [&]()
	{
		const Pending what = m_Pending;
		m_Pending = Pending::None;
		if (what == Pending::Open)
		{
			load_now(m_PendingFile);
		}
		else if (what == Pending::Close)
		{
			m_Editor.reset();
			m_Preview.hide();
			m_Open = false;
		}
		ImGui::CloseCurrentPopup();
	};
	if (ImGui::Button("Save"))
	{
		save();
		if (m_Editor == nullptr || !m_Editor->dirty())
		{
			proceed();
		}
		else
		{
			// the save asked something (a rename) or failed: the user sees that first
			m_Pending = Pending::None;
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard"))
	{
		proceed();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		m_Pending = Pending::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void AssetEditorPanel::on_draw()
{
	if (m_FocusRequested)
	{
		ImGui::SetNextWindowFocus();
		m_FocusRequested = false;
	}
	ImGui::SetNextWindowPos(ImVec2(300.0f, 80.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1000.0f, 620.0f), ImGuiCond_FirstUseEver);
	const std::string windowTitle = title();
	bool open = m_Open;
	const bool visible = ImGui::Begin(windowTitle.c_str(), &open);
	m_Focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!visible)
	{
		m_Preview.hide();
		ImGui::End();
		m_Open = open || (m_Editor != nullptr && m_Editor->dirty()); // closing with unsaved changes asks first (below, once visible again)
		return;
	}

	if (!open && m_Editor != nullptr && m_Editor->dirty())
	{
		m_Pending = Pending::Close;
		m_PromptRequested = true;
		open = true;
	}
	else if (!open)
	{
		m_Editor.reset();
		m_Preview.hide();
	}
	m_Open = open;

	if (m_Editor == nullptr)
	{
		if (!m_Error.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Error.c_str());
		}
		else
		{
			ImGui::TextDisabled("Open a skeleton (.wskel), a skinned mesh (.wmesh), a clip (.wanim) an animation graph (.wgraph) or a behaviour tree (.wbt) from the Assets panel.");
		}
		draw_prompt();
		ImGui::End();
		return;
	}

	m_FramesWithEditor++;
	handle_keys();
	draw_toolbar();
	if (!m_Error.empty())
	{
		ImGui::TextColored(error_color(), "%s", m_Error.c_str());
	}
	if (m_ExternalChange)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "The file changed on disk. Reloading loses your changes.");
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			load_now(m_Editor->file());
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep mine"))
		{
			std::error_code time;
			m_FileTime = std::filesystem::last_write_time(m_Editor->file(), time);
			m_ExternalChange = false;
		}
	}
	ImGui::Separator();

	if (m_Editor != nullptr)
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const bool bottom = m_Editor->has_bottom_pane();
		const float topHeight = bottom ? std::max(available.y * 0.5f, 260.0f) : 0.0f;
		ImGui::BeginChild("##preview", ImVec2(available.x * 0.58f, topHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		draw_preview_column();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("##tools", ImVec2(0.0f, topHeight), ImGuiChildFlags_Borders);
		m_Editor->draw_tools();
		ImGui::EndChild();
		if (bottom)
		{
			ImGui::BeginChild("##bottom", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			m_Editor->draw_bottom(ImGui::GetContentRegionAvail());
			ImGui::EndChild();
		}
	}
	draw_prompt();
	ImGui::End();
}
