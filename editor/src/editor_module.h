#pragma once

#include <core/module.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <panels/panels.h>
#include <play/demo_smoke.h>
#include <play/play_mode.h>
#include <scene/scene_editor.h>

#include <asset_pipeline/reference_finder.h>

#include "editor_ui/command_palette.h"
#include "editor_ui/layout_manager.h"
#include "editor_ui/shortcuts.h"

class EditorModule : public wiwa::IModule
{
public:
	virtual ~EditorModule() override = default;

	virtual void init() override;
	virtual void shutdown() override;

	virtual void pre_update(float deltaTime) override;
	virtual void update(float deltaTime) override;
	virtual void post_update(float deltaTime) override;

	virtual void pre_physics_update(float deltaTime) override;
	virtual void physics_update(float deltaTime) override;
	virtual void post_physics_update(float deltaTime) override;

	virtual void pre_render_update(float deltaTime) override;
	virtual void render_update(float deltaTime) override;
	virtual void post_render_update(float deltaTime) override;

	virtual wiwa::UpdateFlags get_update_flags() const { return wiwa::UpdateFlags::ALL; };
	virtual std::string get_name() const { return "Editor"; };

	virtual int get_priority() const { return 0; }

private:
	void dock_space();
	void main_bar();
	void play_toolbar();
	// Packs with the saved build settings into the game's folder, and blocks the editor with a progress popup until it is done.
	void export_button();
	void export_modal();
	// The scene's file actions: the items of the File menu, and the popups that Open and Save As need.
	void scene_menu_items();
	void scene_popups();
	// Saves the scene; asks for a name first if it has none.
	void save_scene();
	void show_scene_status(const std::string& text);
	// --play-smoke: plays the scene, changes the copy, stops, and checks that the scene being edited came back untouched.
	std::string run_play_smoke();
	void set_theme();
	// The Edit menu: undo and redo, and the entity actions the shortcut table knows.
	void edit_menu();
	void register_shortcuts();
	void layout_menu();
	void open_palette();
	// Starts looking for where the asset is used and shows the References window.
	void find_references(const wiwa::AssetRef& ref);
	void references_window();
	void status_bar();
	// Opens a scene file and remembers it for File > Open recent.
	void open_scene(const std::string& path);
	void load_recent_scenes();
	void save_recent_scenes() const;
	void layout_popups();
private:
	ShortcutRegistry m_Shortcuts{};
	LayoutManager m_Layout{};
	CommandPalette m_Palette{};
	wiwa::ReferenceFinder m_References{};
	bool m_ShowReferences = false;
	class AssetBrowserPanel* m_Browser = nullptr; // owned by m_Panels
	std::vector<std::string> m_RecentScenes{};    // most recent first, as the catalog knows them (source paths)
	bool m_SaveLayoutRequested = false;
	char m_LayoutName[64] = "";
	std::string m_LayoutStatus{};
	bool m_ShowCheatsheet = false;
	bool m_EditHeld = false; // an edit scope is open because a widget or the gizmo is held
	std::vector<Ref<Panel>> m_Panels{};
	Scope<BuildSettingsPanel> m_BuildSettings{}; // a popup: not listed in the menus, drawn every frame
	bool m_ExportRequested = false;
	bool m_ExportUnsaved = false;                // the scene had unsaved changes when Export was pressed: they are not in the pack
	bool m_ExportShown = false;                 // the export popup was opened and has not been closed with Close
	bool m_OpenSceneRequested = false;           // popups are opened from update(), not from inside the menu that asked for them
	bool m_SaveAsRequested = false;
	bool m_SaveAsFocus = false;
	std::vector<std::string> m_SceneChoices{};   // the scene files of the project, for Open Scene
	char m_SaveAsName[128] = "";
	std::string m_SceneStatus{};                 // what went wrong with the last scene action, shown in the bar for a few seconds
	double m_SceneStatusUntil = 0.0;
	Scope<wiwa::SceneEditor> m_SceneEditor;
	Scope<wiwa::IPlayHost> m_PlayHost;
	Scope<wiwa::PlayMode> m_PlayMode;
	bool m_StopPlayRequested = false; // a script called quit()
	Scope<wiwa::DemoSmoke> m_DemoSmoke;
	bool m_PlaySmoke = false;
	wiwa::AssetRef m_PendingDefaultMap{}; // input.wsettings' map, applied once the first import scan has cooked it
	class HierarchyPanel* m_Hierarchy = nullptr; // owned by m_Panels
	class UiEditorPanel* m_UiEditor = nullptr; // owned by m_Panels
	bool m_UiEditorSmoke = false;              // --ui-editor-smoke: open a panel in the UI Editor, run for a while, check it is previewing
	class PhysicsPanel* m_PhysicsPanel = nullptr; // owned by m_Panels
	bool m_PhysicsPanelSmoke = false;            // --physics-panel-smoke: draw the Physics panel for a while with ImGui asserts on, edit the matrix, check it was saved
	std::filesystem::path m_PhysicsPanelFile{};
	class InputPanel* m_InputPanel = nullptr;     // owned by m_Panels
	bool m_InputPanelSmoke = false;              // --input-panel-smoke: draw the Input panel for a while with ImGui asserts on, edit a map through the panel, check it was saved
	std::filesystem::path m_InputPanelFile{};
	class AnimationPanel* m_AnimationPanel = nullptr; // owned by m_Panels
	bool m_AnimPanelSmoke = false;               // --anim-panel-smoke: draw the Animation panel for a while with ImGui asserts on, edit a clip event through the panel, check it was saved
	std::filesystem::path m_AnimPanelDir{};
	std::filesystem::path m_UiEditorSmokeFile{};
	wiwa::u32 m_Frame = 0;
	wiwa::u32 m_PlaySmokeFinalFrame = 0;
	Ref<wiwa::Scene> m_PlaySmokeEdit{};
	size_t m_PlaySmokeCount = 0;
	int m_Operation = ImGuizmo::TRANSLATE;
	bool m_LocalSpace = false;
};