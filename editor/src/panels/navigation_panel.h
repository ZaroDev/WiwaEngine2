#pragma once

#include <string>

#include <entt/entt.hpp>

#include <assets/asset_manager.h>
#include <assets/handle.h>
#include <nav/nav_data.h>
#include <nav/nav_mesh_asset.h>
#include <nav/nav_query.h>
#include <nav/nav_view.h>

#include "../asset_pipeline/asset_database.h"
#include "../asset_pipeline/nav_geometry.h"
#include "../nav/nav_edit_model.h"
#include "../nav/nav_overlay.h"
#include "../nav/nav_path_tool.h"
#include "../scene/scene_editor.h"
#include "../settings/nav_section.h"
#include "panel.h"

// The "Navigation" panel: the NavMeshVolumes of the scene, a Bake button (the bake runs on a worker; the .wnav goes to <assets>/navmeshes and is put in
// the volume through the scene's undoable edit), the numbers of the last bake, a check whether the scene changed since, the walking cost of each kind of
// ground (nav.wsettings), the overlay switches and the path test. Baking and editing are for the edit scene, not while it plays. Closed until opened
// from the Window menu. Main thread, between the ImGui frame's begin and end.
class NavigationPanel final : public Panel
{
public:
	NavigationPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database, wiwa::AssetManager& assets, Ref<NavSection> section);

	virtual void on_update() override;
	virtual void on_draw() override;

	// The volume the panel works on: the selected entity if it is one, else the first of the scene; null if there is none.
	entt::entity current_volume() const;
	// Makes a volume entity at the origin and selects it. Null (entt::null) while the scene plays.
	entt::entity create_volume();
	// Starts the bake of the current volume (what the button does). False, and why in the model's error, if it could not start.
	bool bake();
	wiwa::NavEditModel& model() { return m_Model; }

	// What the viewport shows (the editor wires these into the scene view): the baked mesh of the current volume, the boxes of the volumes and the
	// path test; and the clicks of the scene view as rays while the path test is on (click: start, Shift+click: end). True when the click was used.
	void draw_overlay(wiwa::DebugDraw& draw);
	bool handle_ray_click(glm::vec3 origin, glm::vec3 direction, bool shift);
	wiwa::NavPathTool& path_tool() { return m_PathTool; }

private:
	void finish_bake();
	void refresh_overlay();
	void apply_preset(entt::entity volume, int preset);

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	wiwa::AssetManager& m_Assets;
	Ref<NavSection> m_Section;
	char m_NewAreaName[64]{};
	wiwa::AssetNavMeshSource m_Source;
	wiwa::NavEditModel m_Model;

	// The baked mesh being shown: loaded from the volume's asset, made into Detour data (for the path test) and plain polygons (for drawing and picking).
	wiwa::AssetRef m_OverlayRef{};
	wiwa::Handle<wiwa::NavMeshData> m_OverlayHandle{};
	Scope<wiwa::NavData> m_OverlayData{};
	Scope<wiwa::NavQuery> m_OverlayQuery{};
	wiwa::NavMeshView m_OverlayView{};
	wiwa::NavOverlayShapes m_Shapes{};
	wiwa::NavOverlayOptions m_ShapesOptions{};
	bool m_ShapesValid = false;
	bool m_OverlayDirty = true;
	bool m_PathTest = false;
	wiwa::NavPathTool m_PathTool{};

	wiwa::NavOverlayOptions m_Overlay{};
	bool m_ShowOverlay = true;
	bool m_BakeCollected = true;  // the finished bake has been imported and given to its volume
	std::string m_Message{};      // the last thing worth telling: an import failure, the result of a check
	wiwa::Handle<wiwa::NavMeshData> m_CheckHandle{}; // the stored mesh being loaded for the stale check
	entt::entity m_CheckVolume{ entt::null };
};
