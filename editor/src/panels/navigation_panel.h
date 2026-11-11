#pragma once

#include <string>

#include <entt/entt.hpp>

#include <assets/asset_manager.h>
#include <assets/handle.h>
#include <nav/nav_mesh_asset.h>

#include "../asset_pipeline/asset_database.h"
#include "../asset_pipeline/nav_geometry.h"
#include "../nav/nav_edit_model.h"
#include "../nav/nav_overlay.h"
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
	const wiwa::NavOverlayOptions& overlay_options() const { return m_Overlay; }
	bool overlay_visible() const { return m_ShowOverlay; }

	wiwa::u32 frames_drawn() const { return m_Frames; }
	void request_focus() { m_Focus = true; }

private:
	void finish_bake();
	void apply_preset(entt::entity volume, int preset);

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	wiwa::AssetManager& m_Assets;
	Ref<NavSection> m_Section;
	wiwa::AssetNavMeshSource m_Source;
	wiwa::NavEditModel m_Model;

	wiwa::NavOverlayOptions m_Overlay{};
	bool m_ShowOverlay = true;
	bool m_BakeCollected = true;  // the finished bake has been imported and given to its volume
	std::string m_Message{};      // the last thing worth telling: an import failure, the result of a check
	wiwa::Handle<wiwa::NavMeshData> m_CheckHandle{}; // the stored mesh being loaded for the stale check
	entt::entity m_CheckVolume{ entt::null };
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;
};
