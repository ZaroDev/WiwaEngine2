#pragma once

#include "asset_pipeline/reference_updater.h"

#include <core/module.h>

#include <functional>

#include <imgui.h>
#include <ImGuizmo.h>
#include <panels/panels.h>
#include <play/play_mode.h>
#include <scene/scene_editor.h>
#include "project/starter_scene.h"

#include <asset_pipeline/reference_finder.h>

#include <asset_editor/editor_state.h>
#include <asset_editor/skeleton_index.h>
#include <asset_editor/thumbnail_cache.h>

#include "nav/nav_bake_command.h"

#include "editor_ui/command_palette.h"
#include "editor_ui/layout_manager.h"
#include "editor_ui/shortcuts.h"

class EditorModule;

// What the test project (wiwa_tests) changes about an editor it hosts. The editor passes this to nothing of its own: the default is the editor as it ships.
struct EditorTestSetup
{
	// Where the Physics, Input and Navigation panels save their settings; empty: the project's own file. A smoke edits them, and must not touch the project.
	std::filesystem::path PhysicsSettings{};
	std::filesystem::path InputSettings{};
	std::filesystem::path NavSettings{};
	bool NavSettingsApplyToEngine = true; // false: the Navigation settings are only saved, the running module is left alone
	bool OpenAudioPanel = false;           // the Audio and Navigation panels start closed: a smoke that draws them opens them
	bool OpenNavPanel = false;
	std::function<void(EditorModule&)> OnFrame{}; // called once per frame from update(), before the editor draws its windows
};

class EditorModule : public wiwa::IModule
{
public:
	EditorModule() = default;
	explicit EditorModule(EditorTestSetup test) : m_Test(std::move(test)) {}
	virtual ~EditorModule() override = default;

	virtual void init() override;
	virtual void shutdown() override;

	virtual void pre_update(float deltaTime) override;
	virtual void update(float deltaTime) override;
	virtual void post_update(float deltaTime) override;

	virtual void pre_render_update(float deltaTime) override;
	virtual void render_update(float deltaTime) override;
	virtual void post_render_update(float deltaTime) override;

	virtual wiwa::UpdateFlags get_update_flags() const { return wiwa::UpdateFlags::ALL; };
	virtual std::string get_name() const { return "Editor"; };

	virtual int get_priority() const { return wiwa::ModulePriority::Editor; }

	// Runs `action` on the main thread, from update(), once the first asset scan has finished (on a cold Library the scan is still cooking what the
	// action needs). Actions run in the order they were added; one added after the scan has finished runs at the next update().
	void when_scan_finished(std::function<void()> action) { m_AfterScan.push_back(std::move(action)); }

	wiwa::SceneEditor& scene_editor() { return *m_SceneEditor; }
	// Where the starter scene of a new project is (project/starter_scene.h): NotYet until it settles, then what it settled on.
	wiwa::StarterSceneStep starter_scene_step() const { return m_StarterScene; }

private:
	friend class EditorSmokeDriver; // wiwa_tests: the panel, play and demo smokes
	EditorTestSetup m_Test{};
	wiwa::StarterSceneStep m_StarterScene = wiwa::StarterSceneStep::NotYet;
	std::optional<std::chrono::steady_clock::time_point> m_StarterSceneWaitStart{}; // the first NotYet after the first scan finished

	void dock_space();
	void main_bar();
	void play_toolbar();
	// Packs with the saved build settings into the game's folder, and blocks the editor with a progress popup until it is done.
	void export_button();
	void export_modal();
	void reimport_modal();
	Ref<wiwa::JobProgress> m_ReimportShownJob{}; // the reimport job the modal was opened for
	bool m_ReimportShown = false;
	std::vector<std::string> m_ReimportErrors{};
	// The scene's file actions: the items of the File menu, and the popups that Open and Save As need.
	void scene_menu_items();
	void scene_popups();
	// Saves the scene; asks for a name first if it has none.
	void save_scene();
	void show_scene_status(const std::string& text);
	void set_theme();
	// The Edit menu: undo and redo, and the entity actions the shortcut table knows.
	void edit_menu();
	void register_shortcuts();
	void layout_menu();
	void open_palette();
	// Starts looking for where the asset is used and shows the References window.
	void find_references(const wiwa::AssetRef& ref);
	void references_window();
	void reference_update_popup();
	void status_bar();
	// Opens a scene file and remembers it for File > Open recent.
	void open_scene(const std::string& path);
	void load_recent_scenes();
	void save_recent_scenes() const;
	void remember_recent_scene(const std::string& path);
	void layout_popups();
private:
	ShortcutRegistry m_Shortcuts{};
	LayoutManager m_Layout{};
	CommandPalette m_Palette{};
	wiwa::ReferenceFinder m_References{};
	bool m_ShowReferences = false;
	bool m_UpdateReferencesRequested = false;               // File > Update references to extracted assets was chosen
	std::vector<wiwa::ReferenceUpdate> m_UpdateReferencesPlan{};
	std::string m_UpdateReferencesResult{};
	class AssetBrowserPanel* m_Browser = nullptr; // owned by m_Panels
	std::filesystem::path m_RememberedScene{};      // the scene path the recent list last took from the scene editor
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
	Ref<class AudioSection> m_AudioSection{};       // the Audio page of the Settings panel; its settings are applied once the first scan has cooked the banks
	bool m_PendingAudio = false;
	std::vector<std::function<void()>> m_AfterScan{}; // see when_scan_finished()
	class HierarchyPanel* m_Hierarchy = nullptr; // owned by m_Panels
	class UiEditorPanel* m_UiEditor = nullptr; // owned by m_Panels
	class PhysicsPanel* m_PhysicsPanel = nullptr; // owned by m_Panels
	std::filesystem::path m_PhysicsPanelFile{};
	class InputPanel* m_InputPanel = nullptr;     // owned by m_Panels
	class AnimationPanel* m_AnimationPanel = nullptr; // owned by m_Panels
	class AudioPanel* m_AudioPanel = nullptr;         // owned by m_Panels
	class NavigationPanel* m_NavPanel = nullptr;      // owned by m_Panels
	Scope<wiwa::NavBakeCommand> m_NavBake{};           // --nav-bake <file>: bake the first volume of the scene that is loaded at start to this .wnav and exit
	Scope<wiwa::SkeletonIndex> m_SkeletonIndex{};   // which meshes and clips name each skeleton, kept up to date in the background
	Scope<wiwa::EditorState> m_EditorState{};       // Library/editor_state.json
	Scope<wiwa::ThumbnailCache> m_Thumbnails{};     // the thumbnails of the Asset Browser
	class AssetEditorPanel* m_AssetEditor = nullptr; // owned by m_Panels
	wiwa::u32 m_Frame = 0;
	int m_Operation = ImGuizmo::TRANSLATE;
	bool m_LocalSpace = false;
};