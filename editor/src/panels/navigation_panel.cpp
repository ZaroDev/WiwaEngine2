#include <editor_common.h>
#include "navigation_panel.h"

#include <algorithm>
#include <cctype>

#include <imgui.h>

#include <renderer/debug/debug_draw.h>
#include <scene/components.h>

#include "../asset_pipeline/nav_settings_file.h"

namespace
{
	// "Navmesh Volume" -> "Navmesh_Volume": what is safe in a file name.
	std::string file_safe(const std::string& name)
	{
		std::string out;
		for (const char c : name)
		{
			out += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') ? c : '_';
		}
		return out.empty() ? std::string("volume") : out;
	}

	struct Preset
	{
		const char* Name;
		float Radius;
		float Height;
		float Climb;
		float Cell;
	};

	constexpr Preset PRESETS[] = {
		{ "Humanoid (0.4 x 1.8 m)", 0.4f, 1.8f, 0.4f, 0.2f },
		{ "Small (0.2 x 0.8 m)", 0.2f, 0.8f, 0.2f, 0.1f },
		{ "Large (1.0 x 3.0 m)", 1.0f, 3.0f, 0.8f, 0.4f },
	};
}

NavigationPanel::NavigationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, wiwa::AssetManager& assets, Ref<NavSection> section)
	: Panel("Navigation", PanelCategory::Window), m_Editor(editor), m_Database(database), m_Assets(assets), m_Section(std::move(section)), m_Source(assets), m_Model(m_Source)
{
	m_Open = false; // opened from the Window menu
}

entt::entity NavigationPanel::current_volume() const
{
	if (m_Editor.get_scene() == nullptr)
	{
		return entt::null;
	}
	entt::registry& registry = m_Editor.scene().registry();
	const entt::entity selected = m_Editor.selected();
	if (selected != entt::null && registry.valid(selected) && registry.all_of<wiwa::NavMeshVolume>(selected))
	{
		return selected;
	}
	entt::entity first = entt::null;
	for (const entt::entity entity : registry.view<wiwa::NavMeshVolume>())
	{
		if (first == entt::null || entt::to_integral(entity) < entt::to_integral(first))
		{
			first = entity;
		}
	}
	return first;
}

entt::entity NavigationPanel::create_volume()
{
	if (m_Editor.get_scene() == nullptr || m_Editor.playing())
	{
		return entt::null;
	}
	const entt::entity entity = m_Editor.create_entity(entt::null);
	if (entity == entt::null)
	{
		return entt::null;
	}
	m_Editor.rename_entity(entity, "Navmesh Volume");
	m_Editor.add_component(entity, "NavMeshVolume");
	m_Editor.select(entity);
	return entity;
}

bool NavigationPanel::bake()
{
	const entt::entity volume = current_volume();
	if (volume == entt::null || m_Editor.playing())
	{
		return false;
	}
	entt::registry& registry = m_Editor.scene().registry();
	const std::string name = registry.all_of<wiwa::Name>(volume) ? registry.get<wiwa::Name>(volume).Value : std::string("volume");
	const std::string id = registry.all_of<wiwa::Id>(volume) ? registry.get<wiwa::Id>(volume).Guid.to_string().substr(0, 8) : std::string("0");
	const std::filesystem::path file = m_Database.get_assets_root() / "navmeshes" / (file_safe(name) + "_" + id + ".wnav");
	if (!m_Model.start_bake(m_Editor.scene(), volume, file))
	{
		m_Message = m_Model.error();
		return false;
	}
	m_Message.clear();
	m_BakeCollected = false;
	return true;
}

void NavigationPanel::finish_bake()
{
	m_BakeCollected = true;
	m_OverlayDirty = true; // a new file: the mesh being shown is read again
	if (m_Model.state() != wiwa::NavEditModel::BakeState::Done)
	{
		return;
	}

	// The file is new or changed: cook it, then give the volume the asset (an edit that can be undone, and that marks the scene to be saved).
	const std::optional<wiwa::AssetRef> ref = m_Database.ensure_imported(m_Model.out_file());
	if (!ref)
	{
		m_Message = "The baked file could not be imported: " + m_Model.out_file().string();
		return;
	}
	if (m_Editor.get_scene() != nullptr && m_Editor.scene().registry().valid(m_Model.volume()) && !m_Editor.playing())
	{
		m_Editor.set_field(m_Model.volume(), "NavMeshVolume", "NavMesh", wiwa::FieldValue(*ref));
	}
}

// Loads the baked mesh of the current volume when it changed, and makes what is drawn and picked from it.
void NavigationPanel::refresh_overlay()
{
	const entt::entity volume = current_volume();
	wiwa::AssetRef wanted{};
	if (volume != entt::null)
	{
		wanted = m_Editor.scene().registry().get<wiwa::NavMeshVolume>(volume).NavMesh;
	}

	if (m_OverlayDirty || !(wanted == m_OverlayRef))
	{
		m_OverlayDirty = false;
		m_OverlayRef = wanted;
		m_OverlayHandle = wanted.valid() ? m_Assets.load<wiwa::NavMeshData>(wanted) : wiwa::Handle<wiwa::NavMeshData>{};
		m_OverlayQuery.reset();
		m_OverlayData.reset();
		m_OverlayView = {};
		m_Shapes = {};
		m_ShapesValid = false;
		m_PathTool.clear();
	}

	if (m_OverlayHandle && !m_OverlayData && m_OverlayHandle.ready())
	{
		std::string error;
		m_OverlayData = wiwa::NavData::create(*m_OverlayHandle.get(), error);
		if (m_OverlayData)
		{
			m_OverlayQuery = CreateScope<wiwa::NavQuery>(*m_OverlayData);
			m_OverlayView = wiwa::extract_navmesh_view(*m_OverlayData);
			m_ShapesValid = false;
		}
		else
		{
			m_Message = "The baked navmesh cannot be shown: " + error;
		}
	}

	if (!m_ShapesValid || m_ShapesOptions.Polys != m_Overlay.Polys || m_ShapesOptions.Edges != m_Overlay.Edges || m_ShapesOptions.Boundary != m_Overlay.Boundary)
	{
		m_Shapes = wiwa::nav_overlay_shapes(m_OverlayView, m_Overlay);
		m_ShapesOptions = m_Overlay;
		m_ShapesValid = true;
	}
}

void NavigationPanel::draw_overlay(wiwa::DebugDraw& draw)
{
	if (!m_ShowOverlay || m_Editor.get_scene() == nullptr)
	{
		return;
	}

	// The volumes as boxes; the one the panel works on stands out.
	entt::registry& registry = m_Editor.scene().registry();
	const entt::entity current = current_volume();
	for (auto [entity, volume, world] : registry.view<wiwa::NavMeshVolume, wiwa::WorldTransform>().each())
	{
		const glm::vec3 centre(world.Matrix[3]);
		const glm::vec4 color = entity == current ? glm::vec4(0.3f, 0.9f, 1.0f, 1.0f) : glm::vec4(0.3f, 0.6f, 0.8f, 0.5f);
		draw.box(centre - volume.Size * 0.5f, centre + volume.Size * 0.5f, color, wiwa::DebugDepth::Faded, entity == current ? 2.0f : 1.0f);
	}

	wiwa::draw_nav_overlay(draw, m_Shapes);
	if (m_PathTest)
	{
		m_PathTool.draw(draw);
	}
}

bool NavigationPanel::handle_ray_click(glm::vec3 origin, glm::vec3 direction, bool shift)
{
	if (!m_PathTest || !m_OverlayQuery || m_OverlayView.empty())
	{
		return false;
	}
	const std::optional<glm::vec3> hit = wiwa::NavPathTool::pick(m_OverlayView, origin, direction);
	if (!hit)
	{
		return true; // the path test is on: a click that misses the mesh selects nothing either
	}
	if (shift)
	{
		m_PathTool.set_end(*hit);
	}
	else
	{
		m_PathTool.set_start(*hit);
	}
	m_PathTool.recompute(*m_OverlayQuery, wiwa::NavFilter{ 0xFFFFFFFFu, m_Section->settings().AreaCost });
	return true;
}

void NavigationPanel::on_update()
{
	if (m_Editor.get_scene() != nullptr)
	{
		refresh_overlay();
	}
	m_Model.poll();
	if (!m_BakeCollected && m_Model.state() != wiwa::NavEditModel::BakeState::Running)
	{
		finish_bake();
	}

	// The stale check waits for the stored mesh to load.
	if (m_CheckVolume != entt::null && m_CheckHandle)
	{
		if (m_CheckHandle.state() == wiwa::LoadState::Failed)
		{
			m_Message = "The baked navmesh cannot be loaded: bake it again.";
			m_CheckVolume = entt::null;
		}
		else if (m_CheckHandle.ready() && m_Editor.get_scene() != nullptr && m_Editor.scene().registry().valid(m_CheckVolume))
		{
			const std::optional<bool> stale = m_Model.is_stale(m_Editor.scene(), m_CheckVolume, *m_CheckHandle.get());
			m_Message = !stale ? "The volume cannot be checked (nothing is in it)." : (*stale ? "The scene changed since the bake: bake again." : "The bake is up to date.");
			m_CheckVolume = entt::null;
		}
	}
}

void NavigationPanel::apply_preset(entt::entity volume, int preset)
{
	const Preset& p = PRESETS[preset];
	m_Editor.begin_edit_scope();
	m_Editor.set_field(volume, "NavMeshVolume", "AgentRadius", wiwa::FieldValue(p.Radius));
	m_Editor.set_field(volume, "NavMeshVolume", "AgentHeight", wiwa::FieldValue(p.Height));
	m_Editor.set_field(volume, "NavMeshVolume", "AgentMaxClimb", wiwa::FieldValue(p.Climb));
	m_Editor.set_field(volume, "NavMeshVolume", "CellSize", wiwa::FieldValue(p.Cell));
	m_Editor.set_field(volume, "NavMeshVolume", "CellHeight", wiwa::FieldValue(p.Cell * 0.5f));
	m_Editor.end_edit_scope();
}

void NavigationPanel::on_draw()
{
	m_Frames++; // counted even while the window is a hidden tab: the smoke check wants to know the panel was run
	if (m_Focus)
	{
		ImGui::SetNextWindowFocus();
		m_Focus = false;
	}
	ImGui::SetNextWindowPos(ImVec2(984.0f, 60.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const bool playing = m_Editor.playing();
		const entt::entity volume = current_volume();
		if (m_Editor.get_scene() == nullptr)
		{
			ImGui::TextDisabled("No scene");
		}
		else
		{
			entt::registry& registry = m_Editor.scene().registry();

			if (playing)
			{
				ImGui::TextDisabled("The scene is playing: baking and editing are for the edit scene.");
			}
			ImGui::BeginDisabled(playing);
			if (ImGui::Button("Create volume"))
			{
				create_volume();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::TextDisabled("%zu volume(s)", registry.view<wiwa::NavMeshVolume>().size());

			if (volume == entt::null)
			{
				ImGui::TextWrapped("The scene has no NavMeshVolume. Create one, size it round the walkable ground, and bake.");
			}
			else
			{
				// The volumes: pick one (it becomes the selection).
				if (ImGui::BeginListBox("##volumes", ImVec2(-FLT_MIN, 4.0f * ImGui::GetTextLineHeightWithSpacing())))
				{
					for (const entt::entity entity : registry.view<wiwa::NavMeshVolume>())
					{
						const wiwa::Name* name = registry.try_get<wiwa::Name>(entity);
						const std::string label = (name != nullptr ? name->Value : std::string("Volume")) + "##" + std::to_string(entt::to_integral(entity));
						if (ImGui::Selectable(label.c_str(), entity == volume))
						{
							m_Editor.select(entity);
						}
					}
					ImGui::EndListBox();
				}

				const wiwa::NavMeshVolume& settings = registry.get<wiwa::NavMeshVolume>(volume);
				ImGui::Separator();
				ImGui::Text("Baked mesh: %s", settings.NavMesh.valid() ? settings.NavMesh.Guid.to_string().c_str() : "none");
				ImGui::TextDisabled("Agent %.2f x %.2f m, climb %.2f, slope %.0f, cell %.2f", settings.AgentRadius, settings.AgentHeight, settings.AgentMaxClimb, settings.AgentMaxSlope, settings.CellSize);
				ImGui::TextDisabled("Edit the volume's size and settings in the Inspector.");

				if (ImGui::BeginCombo("Preset", "Apply a preset..."))
				{
					for (int i = 0; i < static_cast<int>(std::size(PRESETS)); i++)
					{
						if (ImGui::Selectable(PRESETS[i].Name) && !playing)
						{
							apply_preset(volume, i);
						}
					}
					ImGui::EndCombo();
				}

				const bool running = m_Model.state() == wiwa::NavEditModel::BakeState::Running;
				ImGui::BeginDisabled(playing || running);
				if (ImGui::Button("Bake"))
				{
					bake();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!running);
				if (ImGui::Button("Cancel"))
				{
					m_Model.cancel();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(playing || running || !settings.NavMesh.valid() || m_CheckVolume != entt::null);
				if (ImGui::Button("Check for changes"))
				{
					m_CheckHandle = m_Assets.load<wiwa::NavMeshData>(settings.NavMesh);
					m_CheckVolume = volume;
					m_Message = "Checking...";
				}
				ImGui::EndDisabled();

				if (running || m_Model.state() != wiwa::NavEditModel::BakeState::Idle)
				{
					ImGui::ProgressBar(m_Model.progress(), ImVec2(-FLT_MIN, 0.0f), m_Model.phase().c_str());
				}
				if (!m_Message.empty())
				{
					ImGui::TextWrapped("%s", m_Message.c_str());
				}

				if (m_Model.state() == wiwa::NavEditModel::BakeState::Done)
				{
					const wiwa::NavBakeResult& last = m_Model.last();
					ImGui::Text("Last bake: %u tiles, %u polygons, %u vertices, %.2f s", last.TileCount, last.PolyCount, last.VertCount, last.Seconds);
				}
				else if (m_Model.state() == wiwa::NavEditModel::BakeState::Failed && !m_Model.error().empty())
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Bake failed: %s", m_Model.error().c_str());
				}
				const std::string log = m_Model.log();
				if (!log.empty())
				{
					ImGui::BeginChild("##navlog", ImVec2(0.0f, 5.0f * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders);
					ImGui::TextUnformatted(log.c_str());
					ImGui::EndChild();
				}
			}

			ImGui::SeparatorText("View");
			ImGui::Checkbox("Show the navmesh in the viewport", &m_ShowOverlay);
			ImGui::Checkbox("Polygons", &m_Overlay.Polys);
			ImGui::SameLine();
			ImGui::Checkbox("Edges", &m_Overlay.Edges);
			ImGui::SameLine();
			ImGui::Checkbox("Boundary", &m_Overlay.Boundary);

			ImGui::Checkbox("Path test (click: start, Shift+click: end)", &m_PathTest);
			if (m_PathTest)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Clear"))
				{
					m_PathTool.clear();
				}
				if (!m_OverlayQuery)
				{
					ImGui::TextDisabled("Bake the volume first: the test walks the baked mesh.");
				}
				else if (m_PathTool.has_start() && m_PathTool.has_end())
				{
					const wiwa::NavPath& path = m_PathTool.path();
					ImGui::Text("%s, %.2f m, %zu corners", path.Status == wiwa::NavPathStatus::Complete ? "Complete" : (path.Status == wiwa::NavPathStatus::Partial ? "Partial: the end is not reachable" : "No route"), path.Length, path.Corners.size());
				}
			}

			ImGui::SeparatorText("Walking cost");
			ImGui::TextDisabled("What one metre of each kind of ground costs; the walk takes the cheaper way round.");
			for (const wiwa::NavArea area : { wiwa::NavArea::Mud, wiwa::NavArea::Water, wiwa::NavArea::Custom0, wiwa::NavArea::Custom1, wiwa::NavArea::Custom2, wiwa::NavArea::Custom3 })
			{
				float cost = m_Section->settings().AreaCost[static_cast<size_t>(area)];
				if (ImGui::DragFloat(wiwa::nav_area_name(area), &cost, 0.1f, 1.0f, wiwa::NAV_AREA_COST_MAX, "%.1f"))
				{
					m_Section->set_cost(area, cost);
				}
			}
			if (!m_Section->last_error().empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Section->last_error().c_str());
			}
		}
	}
	ImGui::End();
}
