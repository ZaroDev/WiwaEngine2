#include "editor_module.h"

#include <fstream>
#include <sstream>

#include <input/input_map_io.h>

#include <asset_pipeline/asset_pipeline_module.h>
#include <asset_pipeline/asset_templates.h>
#include <asset_pipeline/input_settings_file.h>
#include <asset_pipeline/meta_file.h>
#include <asset_pipeline/physics_settings_file.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <input/input_module.h>
#include <physics/physics_module.h>
#include <scene/scene_module.h>
#include <renderer/window.h>
#include <scripting/script_module.h>
#include <ui/ui_module.h>

#include "animation/animation_edit_model.h"
#include "animation/animation_panel.h"
#include "editor_ui/history_panel.h"
#include "ui/ui_editor_panel.h"
#include "ui/ui_smoke_test.h"

#include <imgui.h>
#include <imgui_internal.h>


namespace
{
	// The editor's side of play mode: the scene editor, the scene module, the scripts and the game clock.
	class EditorPlayHost final : public wiwa::IPlayHost
	{
	public:
		EditorPlayHost(wiwa::SceneEditor& editor, wiwa::SceneModule& scenes, wiwa::ScriptingModule& scripts)
			: m_Editor(editor), m_Scenes(scenes), m_Scripts(scripts) {}

		virtual Ref<wiwa::Scene> edit_scene() override { return m_Editor.get_scene(); }
		// not while a scene is opening, a prefab expansion is pending, any scene load is running, or the editor still shows an older scene than
		// the module's (a load that just finished): the copy would be of the wrong scene
		virtual bool can_play() override
		{
			return !m_Editor.is_opening() && !m_Editor.has_pending() && !m_Scenes.is_loading() && m_Editor.get_scene() == m_Scenes.get_active();
		}
		virtual wiwa::EditorDocument capture_document() override { return m_Editor.capture_document(); }

		virtual void show_scene(Ref<wiwa::Scene> scene, bool playing) override
		{
			m_Scenes.set_active(scene);
			m_Editor.set_playing(playing);
			if (playing)
			{
				const wiwa::EditorDocument shown = m_Editor.capture_document();
				m_Editor.set_scene(scene);
				m_Editor.select(shown.Selected.valid() ? scene->find(shown.Selected) : entt::entity{ entt::null }); // the same entity in the copy
			}
		}

		virtual void restore_document(Ref<wiwa::Scene> scene, const wiwa::EditorDocument& document) override { m_Editor.restore_document(std::move(scene), document); }
		virtual Ref<wiwa::Scene> active_scene() override { return m_Scenes.get_active(); }
		// the game UI reads the editor's mouse while the game runs, and no input otherwise
		virtual void start_physics() override
		{
			if (wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
			{
				physics->start_world(m_Scenes.get_active());
			}
		}
		virtual void stop_physics() override
		{
			if (wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
			{
				physics->stop_world();
			}
		}
		virtual void start_scripts() override { set_ui_input(input() != nullptr ? &input()->ui_source() : nullptr); m_Scripts.start(); }
		virtual void stop_scripts() override
		{
			m_Scripts.stop();
			set_ui_input(nullptr);
			if (input() != nullptr)
			{
				input()->set_capture(false); // ImGui has the keyboard and the mouse again
			}
		}
		virtual void cancel_loads() override { m_Scenes.cancel_loads(); }
		virtual void time_play() override { wiwa::Engine::get().get_time().play_game(); }
		virtual void time_stop() override { wiwa::Engine::get().get_time().stop_game(); }
		virtual void time_pause(bool paused) override { wiwa::Engine::get().get_time().set_game_pause(paused); }

	private:
		static wiwa::InputModule* input() { return wiwa::Engine::get().get_module<wiwa::InputModule>(); }

		static void set_ui_input(wiwa::IUiInputSource* source)
		{
			if (wiwa::UiModule* ui = wiwa::Engine::get().get_module<wiwa::UiModule>())
			{
				ui->set_input_source(source);
			}
		}

		wiwa::SceneEditor& m_Editor;
		wiwa::SceneModule& m_Scenes;
		wiwa::ScriptingModule& m_Scripts;
	};
}

void EditorModule::init()
{
	wiwa::Engine::get().get_module<wiwa::Window>()->set_title(ProjectSection::editor_title(wiwa::Engine::get().project()));

	// ImGui owns the keyboard and the mouse; the Viewport hands them to the game while a scene plays
	wiwa::Engine::get().get_module<wiwa::InputModule>()->set_capture(false);

	// scripts run in play mode only; editing a scene does not run them
	wiwa::Engine::get().get_module<wiwa::ScriptingModule>()->set_auto_start(false);
	wiwa::Engine::get().get_time().stop_game(); // and there is no game time either
	wiwa::Engine::get().get_module<wiwa::PhysicsModule>()->set_auto_start(false); // nor a physics world: play mode makes one from its clone

	// The default input map the project chose (input.wsettings); a game gets it from its pack.
	{
		using wiwa::LogLevel;
		wiwa::InputProjectSettings inputSettings;
		std::string inputError;
		if (!wiwa::load_input_settings(wiwa::INPUT_SETTINGS_FILE, inputSettings, inputError))
		{
			WI_LOG(Warning, "Editor", "{0}: {1}", wiwa::INPUT_SETTINGS_FILE, inputError);
		}
		else if (inputSettings.DefaultMap.valid())
		{
			// Not yet: on a cold Library the first scan has not cooked it, and the load would fail (see update()).
			m_PendingDefaultMap = inputSettings.DefaultMap;
		}
	}

	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	m_SceneEditor = CreateScope<wiwa::SceneEditor>(*wiwa::Engine::get().get_module<wiwa::AssetManager>(), *scenes);
	m_SceneEditor->set_scene(scenes->get_active());
	m_SceneEditor->set_follow_module(true);

	m_PlayHost = CreateScope<EditorPlayHost>(*m_SceneEditor, *scenes, *wiwa::Engine::get().get_module<wiwa::ScriptingModule>());
	m_PlayMode = CreateScope<wiwa::PlayMode>(*m_PlayHost);
	// quit() in a script ends play mode, not the editor; the stop waits for post_update because the call comes from inside a script hook
	wiwa::Engine::get().get_module<wiwa::ScriptingModule>()->set_quit_handler([this]() { m_StopPlayRequested = true; });
	for (const std::string& argument : wiwa::Engine::get().get_arguments())
	{
		m_PlaySmoke = m_PlaySmoke || argument == "--play-smoke";
		m_UiEditorSmoke = m_UiEditorSmoke || argument == "--ui-editor-smoke";
		m_PhysicsPanelSmoke = m_PhysicsPanelSmoke || argument == "--physics-panel-smoke";
		m_InputPanelSmoke = m_InputPanelSmoke || argument == "--input-panel-smoke";
		m_AnimPanelSmoke = m_AnimPanelSmoke || argument == "--anim-panel-smoke";
		if (argument == "--demo-smoke")
		{
			m_DemoSmoke = CreateScope<wiwa::DemoSmoke>(*m_SceneEditor, *m_PlayMode);
		}
	}

	Ref<HierarchyPanel> hierarchy = CreateRef<HierarchyPanel>(*m_SceneEditor, wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());
	m_Hierarchy = hierarchy.get(); // owned by m_Panels
	m_Panels.push_back(hierarchy);
	Ref<InspectorPanel> inspector = CreateRef<InspectorPanel>(*m_SceneEditor, wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());
	m_Panels.push_back(inspector);
	m_Panels.push_back(CreateRef<WorldPanel>(*m_SceneEditor));
	// the smoke edits the matrix: it must not touch the project's physics.wsettings
	m_PhysicsPanelFile = m_PhysicsPanelSmoke ? std::filesystem::temp_directory_path() / "wiwa_physics_panel_smoke.wsettings" : std::filesystem::path(wiwa::PHYSICS_SETTINGS_FILE);
	if (m_PhysicsPanelSmoke)
	{
		std::error_code removed;
		std::filesystem::remove(m_PhysicsPanelFile, removed);
	}
	Ref<PhysicsPanel> physicsPanel = CreateRef<PhysicsPanel>(*m_SceneEditor, m_PhysicsPanelFile);
	m_PhysicsPanel = physicsPanel.get();
	m_Panels.push_back(physicsPanel);
	// the smoke check saves a default map: it must not touch the project's input.wsettings
	Ref<InputPanel> inputPanel = CreateRef<InputPanel>(m_InputPanelSmoke ? std::filesystem::temp_directory_path() / "wiwa_input_panel_smoke.wsettings" : std::filesystem::path(wiwa::INPUT_SETTINGS_FILE));
	m_InputPanel = inputPanel.get();
	m_Panels.push_back(inputPanel);
	Ref<AnimationPanel> animationPanel = CreateRef<AnimationPanel>(*m_SceneEditor, wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database(),
		[this]() { return m_PlayMode->state() == wiwa::PlayState::Edit; });
	m_AnimationPanel = animationPanel.get();
	m_Panels.push_back(animationPanel);
	Ref<EditorSection> editorSection = CreateRef<EditorSection>();
	// every project-wide setting in one place (closed until opened from the Window menu); it shares the sections the Physics and Input panels made
	m_Panels.push_back(CreateRef<SettingsPanel>(CreateRef<ProjectSection>(), CreateRef<RendererSection>(), physicsPanel->section(), inputPanel->section(), editorSection));
	// the Scene and Game panels share the renderer's one view: the Game panel, added first, wins when both are visible
	Ref<ViewOwner> viewOwner = CreateRef<ViewOwner>();
	m_Panels.push_back(CreateRef<GamePanel>(*m_SceneEditor, viewOwner));
	Ref<ViewportPanel> viewport = CreateRef<ViewportPanel>(*m_SceneEditor, viewOwner, editorSection);
	m_Panels.push_back(viewport);
	{
		ViewportPanel* viewportPanel = viewport.get(); // owned by m_Panels
		wiwa::SceneEditor* focusEditor = m_SceneEditor.get();
		m_Hierarchy->set_focus([viewportPanel, focusEditor](entt::entity entity) { if (!focusEditor->playing()) { viewportPanel->focus_on(entity); } });
	}
	m_Panels.push_back(CreateRef<PerformancePanel>());
	m_Panels.push_back(CreateRef<ConsolePanel>());
	Ref<MaterialPanel> materialPanel = CreateRef<MaterialPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());
	m_Panels.push_back(materialPanel);
	Ref<AssetBrowserPanel> browser = CreateRef<AssetBrowserPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());
	m_Panels.push_back(browser);
	// the panels live as long as m_Panels, which holds all of them until shutdown
	MaterialPanel* materials = materialPanel.get();
	Ref<UiEditorPanel> uiEditorPanel = CreateRef<UiEditorPanel>(*m_SceneEditor);
	m_Panels.push_back(uiEditorPanel);
	m_Panels.push_back(CreateRef<HistoryPanel>(*m_SceneEditor));
	UiEditorPanel* uiEditor = uiEditorPanel.get();
	m_UiEditor = uiEditor;
	uiEditor->set_maximize([this]() { m_Layout.toggle_maximize(m_UiEditor); }, [this]() { return m_Layout.maximized() == m_UiEditor; });
	AssetBrowserPanel* browserPanel = browser.get();
	m_Browser = browserPanel;
	browser->set_open_material([materials](const std::filesystem::path& wmat) { materials->open(wmat); });
	browser->set_open_ui_panel([uiEditor](const std::filesystem::path& wuipanel) { uiEditor->open(wuipanel); });
	wiwa::SceneEditor* sceneEditor = m_SceneEditor.get();
	browser->set_make_prefab([sceneEditor](entt::entity entity, const std::filesystem::path& folder) { return sceneEditor->make_prefab(entity, folder); });
	inspector->set_locate([browserPanel](const wiwa::AssetRef& ref) { browserPanel->select_asset(ref); });
	inspector->set_find_references([this](const wiwa::AssetRef& ref) { find_references(ref); });
	browserPanel->set_find_references([this](const wiwa::AssetRef& ref) { find_references(ref); });
	m_BuildSettings = CreateScope<BuildSettingsPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());

	m_Layout.init("layouts");
	load_recent_scenes();
	register_shortcuts();
	set_theme();
}

// True when the focused window keeps an undo history of its own and handles Ctrl+Z itself (the UI Editor).
static bool focused_window_has_own_undo()
{
	const ImGuiWindow* window = ImGui::GetCurrentContext()->NavWindow;
	if (window == nullptr || window->RootWindow == nullptr)
	{
		return false;
	}
	const char* name = window->RootWindow->Name;
	return std::strncmp(name, "UI Editor", 9) == 0;
}

void EditorModule::register_shortcuts()
{
	wiwa::SceneEditor* editor = m_SceneEditor.get();
	// while the scene plays the keyboard is the game's
	const auto notPlaying = [editor]() { return !editor->playing(); };
	m_Shortcuts.add({ "edit.undo", "Undo", ImGuiMod_Ctrl | ImGuiKey_Z, [editor]() { editor->undo(); },
		[editor]() { return editor->can_undo() && !focused_window_has_own_undo(); } });
	m_Shortcuts.add({ "edit.redo", "Redo", ImGuiMod_Ctrl | ImGuiKey_Y, [editor]() { editor->redo(); },
		[editor]() { return editor->can_redo() && !focused_window_has_own_undo(); } });
	m_Shortcuts.add({ "edit.redo2", "Redo", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, [editor]() { editor->redo(); },
		[editor]() { return editor->can_redo() && !focused_window_has_own_undo(); } });
	m_Shortcuts.add({ "file.save", "Save scene", ImGuiMod_Ctrl | ImGuiKey_S, [this]() { save_scene(); }, {} });
	m_Shortcuts.add({ "edit.duplicate", "Duplicate", ImGuiMod_Ctrl | ImGuiKey_D,
		[editor]() { editor->duplicate_entity(editor->selected()); },
		[editor]() { return !editor->playing() && editor->selected() != entt::null; } });
	HierarchyPanel* hierarchy = m_Hierarchy;
	const auto hasSelection = [editor]() { return !editor->playing() && editor->selected() != entt::null; };
	m_Shortcuts.add({ "edit.rename", "Rename", ImGuiKey_F2, [hierarchy]() { hierarchy->request_rename(); }, hasSelection });
	m_Shortcuts.add({ "edit.copy", "Copy", ImGuiMod_Ctrl | ImGuiKey_C, [hierarchy]() { hierarchy->copy_selection(); }, [editor]() { return !editor->playing() && editor->selected() != entt::null; } });
	m_Shortcuts.add({ "edit.cut", "Cut", ImGuiMod_Ctrl | ImGuiKey_X, [hierarchy]() { hierarchy->cut_selection(); }, hasSelection });
	m_Shortcuts.add({ "edit.paste", "Paste", ImGuiMod_Ctrl | ImGuiKey_V, [hierarchy]() { hierarchy->paste(); }, [editor]() { return !editor->playing() && editor->has_clipboard(); } });
	m_Shortcuts.add({ "edit.focus", "Focus selection", ImGuiKey_F, [hierarchy]() { hierarchy->focus_selection(); }, [editor]() { return !editor->playing() && editor->selected() != entt::null; } });
	m_Shortcuts.add({ "edit.delete", "Delete", 0, [editor]() { editor->delete_entity(editor->selected()); }, hasSelection });
	m_Shortcuts.add({ "view.maximize", "Maximize panel under the cursor", ImGuiMod_Shift | ImGuiKey_Space,
		[this]() { m_Layout.toggle_maximize(m_Layout.maximized() != nullptr ? m_Layout.maximized() : LayoutManager::panel_at_cursor(m_Panels)); }, notPlaying });
	m_Shortcuts.add({ "view.maximize_ui", "Maximize the UI Editor", ImGuiKey_F11, [this]() { m_Layout.toggle_maximize(m_UiEditor); }, [this]() { return !m_SceneEditor->playing() && m_UiEditor != nullptr && m_UiEditor->is_active(); } });
	m_Shortcuts.add({ "view.palette", "Command palette", ImGuiMod_Ctrl | ImGuiKey_P, [this]() { open_palette(); }, notPlaying });
	m_Shortcuts.add({ "help.shortcuts", "Keyboard shortcuts", ImGuiKey_F1, [this]() { m_ShowCheatsheet = !m_ShowCheatsheet; }, notPlaying });
}

void EditorModule::layout_menu()
{
	if (ImGui::BeginMenu("Layouts"))
	{
		for (const std::string& name : LayoutManager::builtin_names())
		{
			if (ImGui::MenuItem(name.c_str()))
			{
				m_Layout.request_builtin(name);
			}
		}
		const std::vector<std::string> saved = m_Layout.user_names();
		if (!saved.empty())
		{
			ImGui::Separator();
			for (const std::string& name : saved)
			{
				if (ImGui::MenuItem(name.c_str()) && !m_Layout.request_user(name))
				{
					m_LayoutStatus = "The layout " + name + " could not be loaded";
				}
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Save current layout..."))
		{
			m_SaveLayoutRequested = true;
			std::snprintf(m_LayoutName, sizeof(m_LayoutName), "%s", "My layout");
		}
		if (ImGui::BeginMenu("Delete saved layout", !saved.empty()))
		{
			for (const std::string& name : saved)
			{
				if (ImGui::MenuItem(name.c_str()))
				{
					m_Layout.delete_user(name);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Reset layout"))
		{
			m_Layout.request_builtin("Default");
		}
		ImGui::EndMenu();
	}
	const bool maximized = m_Layout.maximized() != nullptr;
	if (ImGui::MenuItem(maximized ? "Restore panel" : "Maximize panel", "Shift+Space"))
	{
		m_Layout.toggle_maximize(maximized ? m_Layout.maximized() : LayoutManager::panel_at_cursor(m_Panels));
	}
}

void EditorModule::layout_popups()
{
	if (m_SaveLayoutRequested)
	{
		m_SaveLayoutRequested = false;
		m_LayoutStatus.clear();
		ImGui::OpenPopup("Save layout");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Save layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		const bool entered = ImGui::InputText("##layout_name", m_LayoutName, sizeof(m_LayoutName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (!m_LayoutStatus.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_LayoutStatus.c_str());
		}
		if (ImGui::Button("Save") || entered)
		{
			if (m_Layout.save_user(m_LayoutName))
			{
				m_LayoutStatus.clear();
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_LayoutStatus = "Use a name without \\ / : * ? \" < > | and not starting or ending with a dot";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_LayoutStatus.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void EditorModule::open_palette()
{
	std::vector<CommandPalette::Item> items;

	for (const ShortcutAction& action : m_Shortcuts.all())
	{
		if (action.Id == "edit.redo2" || action.Id == "view.palette" || !action.Run)
		{
			continue;
		}
		const std::string id = action.Id;
		items.push_back({ action.Label, "Action", ShortcutRegistry::chord_text(action.Chord), [this, id]() { m_Shortcuts.run(id); } });
	}

	for (const Ref<Panel>& panel : m_Panels)
	{
		if (!any(panel->get_category(), PanelCategory::Window | PanelCategory::Info | PanelCategory::Renderer))
		{
			continue;
		}
		Panel* target = panel.get();
		items.push_back({ std::string(panel->get_name()) + " panel", "Panel", panel->is_active() ? "open" : "closed", [target]() { target->switch_active(); } });
	}

	for (const std::string& name : LayoutManager::builtin_names())
	{
		items.push_back({ name + " layout", "Layout", "preset", [this, name]() { m_Layout.request_builtin(name); } });
	}
	for (const std::string& name : m_Layout.user_names())
	{
		items.push_back({ name + " layout", "Layout", "saved", [this, name]() { m_Layout.request_user(name); } });
	}

	if (m_SceneEditor->get_scene() != nullptr)
	{
		size_t shown = 0;
		wiwa::Scene& scene = m_SceneEditor->scene();
		scene.registry().view<wiwa::Name, wiwa::Id>().each([&](entt::entity, const wiwa::Name& name, const wiwa::Id& id)
		{
			if (shown++ < 2000)
			{
				const wiwa::EntityGuid guid = id.Guid;
				items.push_back({ name.Value.empty() ? "(unnamed)" : name.Value, "Entity", "", [this, guid]()
				{
					const entt::entity entity = m_SceneEditor->get_scene() != nullptr ? m_SceneEditor->scene().find(guid) : entt::entity{ entt::null };
					if (entity != entt::null)
					{
						m_SceneEditor->select(entity);
						m_Hierarchy->focus_selection();
					}
				} });
			}
		});
	}

	if (wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>())
	{
		for (const wiwa::AssetCatalog::Entry& entry : assets->catalog_entries())
		{
			const wiwa::AssetRef ref{ entry.Guid, 0 };
			items.push_back({ entry.Path, "Asset", "", [this, ref]() { m_Browser->select_asset(ref); } });
		}
	}

	m_Palette.open(std::move(items));
}

void EditorModule::find_references(const wiwa::AssetRef& ref)
{
	if (!ref.valid())
	{
		return;
	}
	std::vector<std::filesystem::path> roots;
	for (const wiwa::AssetDatabase::SourceRoot& root : wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database().get_roots())
	{
		roots.push_back(root.Source);
	}
	const std::optional<std::filesystem::path> source = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database().path_of(ref.Guid);
	m_References.start(ref.Guid, source ? source->filename().generic_string() : std::string(), std::move(roots));
	m_ShowReferences = true;
}

void EditorModule::references_window()
{
	if (!m_ShowReferences)
	{
		return;
	}

	wiwa::AssetDatabase& database = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database();
	const std::optional<std::filesystem::path> target = database.path_of(m_References.target());
	const std::string title = "References to " + (target ? target->filename().string() : m_References.target().to_string().substr(0, 8)) + "###References";

	ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(title.c_str(), &m_ShowReferences))
	{
		if (!m_References.finished())
		{
			ImGui::ProgressBar(m_References.progress(), ImVec2(-1.0f, 0.0f), "Searching...");
		}

		const std::vector<wiwa::ReferenceHit> hits = m_References.hits();
		if (m_References.finished() && hits.empty())
		{
			ImGui::TextDisabled("No references: nothing in the project uses this asset");
		}
		for (const wiwa::ReferenceHit& hit : hits)
		{
			const std::string label = hit.File.filename().string() + "  (" + std::to_string(hit.Count) + (hit.Count == 1 ? " use)" : " uses)");
			if (ImGui::Selectable(label.c_str()))
			{
				if (wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>())
				{
					if (const std::optional<wiwa::AssetRef> found = assets->find_asset(hit.File.string()))
					{
						m_Browser->select_asset(*found);
					}
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s\nDouble-click opens a scene or a UI panel", hit.File.generic_string().c_str());
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				const std::string extension = hit.File.extension().string();
				if (extension == ".wscene" && !m_SceneEditor->playing())
				{
					open_scene(hit.File.string());
				}
				else if (extension == ".wuipanel")
				{
					m_UiEditor->open(hit.File);
				}
			}
		}
	}
	ImGui::End();
}

void EditorModule::status_bar()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;
	if (ImGui::BeginViewportSideBar("##status_bar", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), flags))
	{
		if (ImGui::BeginMenuBar())
		{
			const std::string scene = m_SceneEditor->path().empty() ? "untitled" : m_SceneEditor->path().filename().string();
			ImGui::Text("%s%s", scene.c_str(), m_SceneEditor->dirty() ? " *" : "");

			const entt::entity selected = m_SceneEditor->selected();
			if (m_SceneEditor->get_scene() != nullptr && selected != entt::null && m_SceneEditor->scene().registry().valid(selected))
			{
				ImGui::TextDisabled("|");
				ImGui::Text("%s", m_SceneEditor->scene().registry().get<wiwa::Name>(selected).Value.c_str());
			}

			const std::string right = std::to_string(m_SceneEditor->history().undo_labels().size()) + " undo steps   " + std::to_string(static_cast<int>(ImGui::GetIO().Framerate)) + " FPS";
			ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - ImGui::GetStyle().WindowPadding.x * 2.0f);
			ImGui::TextDisabled("%s", right.c_str());
			ImGui::EndMenuBar();
		}
	}
	ImGui::End();
}

void EditorModule::open_scene(const std::string& path)
{
	m_SceneEditor->open(path);
	m_RecentScenes.erase(std::remove(m_RecentScenes.begin(), m_RecentScenes.end(), path), m_RecentScenes.end());
	m_RecentScenes.insert(m_RecentScenes.begin(), path);
	if (m_RecentScenes.size() > 8)
	{
		m_RecentScenes.resize(8);
	}
	save_recent_scenes();
}

void EditorModule::load_recent_scenes()
{
	std::ifstream in("recent_scenes.txt");
	std::string line;
	while (std::getline(in, line) && m_RecentScenes.size() < 8)
	{
		if (!line.empty())
		{
			m_RecentScenes.push_back(line);
		}
	}
}

void EditorModule::save_recent_scenes() const
{
	std::ofstream out("recent_scenes.txt", std::ios::trunc);
	for (const std::string& scene : m_RecentScenes)
	{
		out << scene << '\n';
	}
}

void EditorModule::edit_menu()
{
	if (!ImGui::BeginMenu("Edit"))
	{
		return;
	}

	const wiwa::UndoStack& history = m_SceneEditor->history();
	const std::string undoText = m_SceneEditor->can_undo() ? "Undo: " + history.undo_label() : std::string("Undo");
	const std::string redoText = m_SceneEditor->can_redo() ? "Redo: " + history.redo_label() : std::string("Redo");
	if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, m_SceneEditor->can_undo()))
	{
		m_SceneEditor->undo();
	}
	if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, m_SceneEditor->can_redo()))
	{
		m_SceneEditor->redo();
	}
	ImGui::Separator();
	for (const char* id : { "edit.duplicate", "edit.copy", "edit.cut", "edit.paste", "edit.rename", "edit.delete" })
	{
		if (const ShortcutAction* action = m_Shortcuts.find(id))
		{
			const std::string chord = ShortcutRegistry::chord_text(action->Chord);
			if (ImGui::MenuItem(action->Label.c_str(), chord.c_str(), false, m_Shortcuts.enabled(*action)))
			{
				action->Run();
			}
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Keyboard shortcuts", "F1"))
	{
		m_ShowCheatsheet = true;
	}
	ImGui::EndMenu();
}

void EditorModule::shutdown()
{
	// leaving while the scene plays: the scripts end and the scene that was being edited is the one that is left
	if (m_PlayMode != nullptr)
	{
		m_PlayMode->stop();
	}
}

void EditorModule::pre_update(float deltaTime)
{
}

void EditorModule::update(float deltaTime)
{
	m_Frame++;
	if (m_PendingDefaultMap.valid())
	{
		const Ref<wiwa::JobProgress> scan = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database().scan_progress();
		if (scan && scan->finished())
		{
			wiwa::Engine::get().get_module<wiwa::InputModule>()->set_default_map(m_PendingDefaultMap);
			m_PendingDefaultMap = {};
		}
	}
	if (!m_SceneEditor->playing() && !wiwa::Engine::get().get_module<wiwa::InputModule>()->backend_injected())
	{
		wiwa::Engine::get().get_module<wiwa::InputModule>()->set_capture(false); // the Viewport turns it on while it draws, in play mode
	}
	if (m_UiEditorSmoke && m_UiEditor != nullptr)
	{
		if (m_Frame == 20)
		{
			m_UiEditorSmokeFile = wiwa::write_ui_editor_smoke_panel();
			m_UiEditor->open(m_UiEditorSmokeFile);
		}
		if (m_Frame == 150)
		{
			const bool passed = m_UiEditor->loaded() && m_UiEditor->previewing();
			WI_INFO("Editor", "UI editor smoke test {0}", passed ? "passed" : "FAILED: the panel did not load or is not previewing");
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_PhysicsPanelSmoke && m_PhysicsPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_PhysicsPanel->request_focus();
		}
		if (m_Frame == 40)
		{
			// a click in the matrix: layers 0 and 1 stop colliding (a layer is named first so that the matrix shows it)
			m_PhysicsPanel->set_layer_name(1, "Smoke");
			m_PhysicsPanel->set_collides(0, 1, false);
		}
		if (m_Frame == 130)
		{
			wiwa::PhysicsLayers saved;
			std::string error;
			const bool read = wiwa::load_physics_layers(m_PhysicsPanelFile, saved, error);
			wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>();
			const bool passed = m_PhysicsPanel->frames_drawn() >= 100 && read && saved.Names[1] == "Smoke" && !saved.collides(0, 1) && saved.collides(0, 0) && saved.collides(1, 1) &&
				physics != nullptr && !physics->layers().collides(0, 1) && m_PhysicsPanel->last_error().empty();
			WI_INFO("Editor", "Physics panel smoke test {0} (frames {1}, read {2}, layer 1 \"{3}\", 0-1 collide {4}, error \"{5}\")", passed ? "passed" : "FAILED", m_PhysicsPanel->frames_drawn(), read, saved.Names[1], saved.collides(0, 1), m_PhysicsPanel->last_error());
			std::error_code removed;
			std::filesystem::remove(m_PhysicsPanelFile, removed);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_InputPanelSmoke && m_InputPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_InputPanel->request_focus();
		}
		if (m_Frame == 100)
		{
			// open a map, add an action with a binding through the model, and save it
			m_InputPanelFile = std::filesystem::temp_directory_path() / "wiwa_input_panel_smoke.winput";
			{
				std::ofstream out(m_InputPanelFile, std::ios::binary | std::ios::trunc);
				out << R"({"version":1,"maps":[{"name":"Gameplay","actions":[{"name":"Jump","type":"Button","bindings":[{"control":"Key.Space"}]}]}]})";
			}
			if (m_InputPanel->open_path(m_InputPanelFile) && m_InputPanel->model() != nullptr)
			{
				wiwa::InputEditModel* model = m_InputPanel->model();
				wiwa::Binding binding;
				binding.Parts[0] = wiwa::control(wiwa::Key::F9);
				const size_t action = model->add_action(0, "Smoke", wiwa::ActionType::Button);
				model->add_binding(0, action, binding);
				m_InputPanel->save();
			}
		}
		if (m_Frame == 130)
		{
			std::ifstream in(m_InputPanelFile, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			wiwa::InputMapData saved;
			std::string error;
			const bool read = wiwa::input_map_from_text(text.str(), saved, error);
			const wiwa::Action* added = read && !saved.Maps.empty() && saved.Maps[0].Actions.size() == 2 ? &saved.Maps[0].Actions[1] : nullptr;
			const bool passed = m_InputPanel->frames_drawn() >= 100 && read && added != nullptr && added->Name == "Smoke" && added->Bindings.size() == 1 &&
				added->Bindings[0].Parts[0] == wiwa::control(wiwa::Key::F9) && m_InputPanel->last_error().empty() &&
				m_InputPanel->model() != nullptr && !m_InputPanel->model()->dirty();
			WI_INFO("Editor", "Input panel smoke test {0} (frames {1}, read {2}, actions {3}, error \"{4}\")", passed ? "passed" : "FAILED", m_InputPanel->frames_drawn(), read,
				read && !saved.Maps.empty() ? saved.Maps[0].Actions.size() : 0, m_InputPanel->last_error());
			std::error_code removed;
			std::filesystem::remove(m_InputPanelFile, removed);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_AnimPanelSmoke && m_AnimationPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_AnimationPanel->request_focus();
		}
		if (m_Frame == 100)
		{
			// a model with a .meta in a folder of its own: open its clips, add an event through the model and save it
			m_AnimPanelDir = std::filesystem::temp_directory_path() / "wiwa_anim_panel_smoke";
			std::error_code ignored;
			std::filesystem::remove_all(m_AnimPanelDir, ignored);
			std::filesystem::create_directories(m_AnimPanelDir, ignored);
			const std::filesystem::path model = m_AnimPanelDir / "rig.glb";
			{
				std::ofstream out(model, std::ios::binary | std::ios::trunc);
				out << "not really a model";
			}
			wiwa::MetaFile meta;
			meta.Guid = wiwa::AssetGuid::generate();
			meta.Importer = "ModelImporter";
			meta.Settings = nlohmann::json::parse(R"({"generatePhysicsShapes":true,"animationEvents":[{"clip":"Walk","time":0.5,"name":"left"}]})");
			meta.save(wiwa::MetaFile::meta_path_for(model));
			if (m_AnimationPanel->open_events(model, { { "Walk", 2.0f }, { "Run", 1.0f } }) && m_AnimationPanel->model() != nullptr)
			{
				wiwa::AnimationEditModel* events = m_AnimationPanel->model();
				events->add_event(0, 1.5f, "right");
				events->add_event(1, 0.25f, "turn");
				m_AnimationPanel->save();
			}
		}
		if (m_Frame == 130)
		{
			const std::filesystem::path model = m_AnimPanelDir / "rig.glb";
			const std::optional<wiwa::MetaFile> saved = wiwa::MetaFile::load(wiwa::MetaFile::meta_path_for(model));
			bool passed = saved.has_value() && m_AnimationPanel->frames_drawn() >= 100 && m_AnimationPanel->last_error().empty()
				&& m_AnimationPanel->model() != nullptr && !m_AnimationPanel->model()->dirty();
			std::string names;
			if (passed)
			{
				const wiwa::AnimationEditModel reread = wiwa::AnimationEditModel::from_settings({ { "Walk", 2.0f }, { "Run", 1.0f } }, saved->Settings);
				for (const wiwa::EditableClip& clip : reread.clips())
				{
					for (const wiwa::AnimationEvent& event : clip.Events)
					{
						names += clip.Name + "/" + event.Name + " ";
					}
				}
				passed = names == "Walk/left Walk/right Run/turn " && saved->Settings.value("generatePhysicsShapes", false);
			}
			WI_INFO("Editor", "Animation panel smoke test {0} (frames {1}, events \"{2}\", error \"{3}\")", passed ? "passed" : "FAILED", m_AnimationPanel->frames_drawn(), names, m_AnimationPanel->last_error());
			std::error_code ignored;
			std::filesystem::remove_all(m_AnimPanelDir, ignored);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_PlaySmoke && m_Frame == 30)
	{
		const std::string failure = run_play_smoke();
		if (!failure.empty())
		{
			WI_INFO("Editor", "Play smoke test FAILED: {0}", failure);
		}
		if (!failure.empty())
		{
			wiwa::Engine::get().set_exit_code(1);
			wiwa::Engine::get().request_quit();
		}
	}
	if (m_PlaySmoke && m_PlaySmokeFinalFrame != 0 && m_Frame == m_PlaySmokeFinalFrame)
	{
		wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
		std::string failure;
		if (scenes->get_active() != m_PlaySmokeEdit || m_SceneEditor->get_scene() != m_PlaySmokeEdit)
		{
			failure = "a load that outlived Stop replaced the edit scene";
		}
		else if (m_PlaySmokeEdit->registry().storage<wiwa::Id>().size() != m_PlaySmokeCount)
		{
			failure = "a load that outlived Stop added entities to the edit scene";
		}
		WI_INFO("Editor", "{0}", failure.empty() ? std::string("Play smoke test passed") : "Play smoke test FAILED: " + failure);
		wiwa::Engine::get().set_exit_code(failure.empty() ? 0 : 1);
		wiwa::Engine::get().request_quit();
	}

	if (m_DemoSmoke != nullptr && m_Frame >= 30)
	{
		std::string failure;
		if (m_DemoSmoke->update(failure))
		{
			WI_INFO("Editor", "{0}", failure.empty() ? std::string("Demo smoke test passed") : "Demo smoke test FAILED: " + failure);
			wiwa::Engine::get().set_exit_code(failure.empty() ? 0 : 1);
			wiwa::Engine::get().request_quit();
			m_DemoSmoke.reset();
		}
	}

	m_Shortcuts.poll();
	m_Shortcuts.draw_cheatsheet(m_ShowCheatsheet);
	main_bar();
	status_bar();
	m_Palette.draw();
	references_window();
	export_modal();
	scene_popups();
	m_BuildSettings->on_draw();
	dock_space();

	// Adopts the scene the SceneModule loaded (or opened), and expands prefab instances once their prefab is there.
	// Anything held (a slider, a text field, the transform gizmo) is one undo step, committed when it is let go. Opened here, before the scene
	// settles its history, so that the edits of every frame of a drag stay together.
	{
		const bool held = ImGui::IsAnyItemActive() || (ImGuizmo::IsUsing() && ImGui::IsMouseDown(ImGuiMouseButton_Left));
		if (held && !m_EditHeld)
		{
			m_SceneEditor->begin_edit_scope();
			m_EditHeld = true;
		}
		else if (!held && m_EditHeld)
		{
			m_SceneEditor->end_edit_scope();
			m_EditHeld = false;
		}
	}
	m_SceneEditor->update();
	if (const std::string message = m_SceneEditor->take_message(); !message.empty())
	{
		show_scene_status(message);
	}

	for (auto& panel : m_Panels)
	{
		panel->on_update();
	}

	m_Layout.update_maximize(!m_SceneEditor->playing() && !m_Palette.is_open());
	for (auto& panel : m_Panels)
	{
		// a maximized panel is the only one drawn
		if (panel->is_active() && (m_Layout.maximized() == nullptr || m_Layout.maximized() == panel.get()))
		{
			panel->on_draw();
		}
	}
	layout_popups();
}

void EditorModule::post_update(float deltaTime)
{
	// after the scripts ran: a step ends here, and a scene a script loaded becomes the one that plays
	m_PlayMode->update();
	if (m_StopPlayRequested)
	{
		m_StopPlayRequested = false;
		m_PlayMode->stop();
	}
}

void EditorModule::pre_physics_update(float deltaTime)
{
}

void EditorModule::physics_update(float deltaTime)
{
}

void EditorModule::post_physics_update(float deltaTime)
{
}

void EditorModule::pre_render_update(float deltaTime)
{
}

void EditorModule::render_update(float deltaTime)
{
}

void EditorModule::post_render_update(float deltaTime)
{
}

void EditorModule::dock_space()
{
	// Note: Switch this to true to enable dockspace
	static bool dockspaceOpen = true;
	static bool opt_fullscreen_persistant = true;
	bool opt_fullscreen = opt_fullscreen_persistant;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - ImGui::GetFrameHeight()));
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// DockSpace
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	float minWinSizeX = style.WindowMinSize.x;
	style.WindowMinSize.x = 370.0f;
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("DockSpace");
		m_Layout.apply_pending(dockspace_id, m_Panels);
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}


	ImGui::End();
}

void EditorModule::main_bar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			scene_menu_items();
			ImGui::Separator();
			if (ImGui::MenuItem("Build Settings..."))
			{
				m_BuildSettings->open();
			}
			export_button();
			ImGui::EndMenu();
		}

		edit_menu();

		if (ImGui::BeginMenu("Window"))
		{
			layout_menu();
			ImGui::Separator();
			for (auto& panel : m_Panels)
			{
				if (any(panel->get_category(), PanelCategory::Window))
				{
					if (ImGui::MenuItem(panel->get_name(), nullptr, panel->is_active()))
					{
						panel->switch_active();
					}
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Info"))
		{
			for (auto& panel : m_Panels)
			{
				if (any(panel->get_category(), PanelCategory::Info))
				{
					if (ImGui::MenuItem(panel->get_name(), nullptr, panel->is_active()))
					{
						panel->switch_active();
					}
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Renderer"))
		{
			for (auto& panel : m_Panels)
			{
				if (any(panel->get_category(), PanelCategory::Renderer))
				{
					if (ImGui::MenuItem(panel->get_name(), nullptr, panel->is_active()))
					{
						panel->switch_active();
					}
				}
			}
			ImGui::EndMenu();
		}

		ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x / 2 - 15.0f);
		const bool playing = m_PlayMode->state() != wiwa::PlayState::Edit;
		
		// the bar is tinted while the scene plays, so that nobody edits the copy thinking it is the scene
		if (playing)
		{
			ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.55f, 0.28f, 0.05f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{ 0.55f, 0.28f, 0.05f, 1.0f });
		}
		play_toolbar();

		if (playing)
		{
			ImGui::PopStyleColor(2);
		}

		ImGui::EndMainMenuBar();
	}	
}

void EditorModule::play_toolbar()
{
	const wiwa::PlayState state = m_PlayMode->state();
	const bool playing = m_PlayMode->state() != wiwa::PlayState::Edit;
	const char* play = playing ? ICON_FK_STOP : ICON_FK_PLAY;
	if (state == wiwa::PlayState::Edit)
	{
		ImGui::BeginDisabled(!m_PlayHost->can_play());
		if (ImGui::Button(play))
		{
			m_PlayMode->play();
		}
		ImGui::EndDisabled();
	}
	else if (ImGui::Button(play))
	{
		m_PlayMode->stop();
	}

	ImGui::BeginDisabled(state == wiwa::PlayState::Edit);
	if (ImGui::Button(state == wiwa::PlayState::Paused ? ICON_FK_PLAY : ICON_FK_PAUSE))
	{
		if (state == wiwa::PlayState::Paused)
		{
			m_PlayMode->resume();
		}
		else
		{
			m_PlayMode->pause();
		}
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(state != wiwa::PlayState::Paused);
	if (ImGui::Button(ICON_FK_STEP_FORWARD))
	{
		m_PlayMode->step();
	}
	ImGui::EndDisabled();
	

	if (!m_SceneStatus.empty() && ImGui::GetTime() < m_SceneStatusUntil)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "  %s", m_SceneStatus.c_str());
	}
}

void EditorModule::show_scene_status(const std::string& text)
{
	m_SceneStatus = text;
	m_SceneStatusUntil = ImGui::GetTime() + 5.0;
}

void EditorModule::save_scene()
{
	if (m_SceneEditor->playing())
	{
		return;
	}
	if (m_SceneEditor->path().empty())
	{
		m_SaveAsRequested = true;
	}
	else if (!m_SceneEditor->save())
	{
		show_scene_status("Could not save " + m_SceneEditor->path().string() + " (see the log)");
	}
}

void EditorModule::scene_menu_items()
{
	ImGui::BeginDisabled(m_SceneEditor->playing());
	if (ImGui::MenuItem("New Scene"))
	{
		m_SceneEditor->new_scene();
	}
	if (ImGui::MenuItem("Open Scene..."))
	{
		m_OpenSceneRequested = true;
	}
	if (ImGui::BeginMenu("Open Recent", !m_RecentScenes.empty()))
	{
		std::string chosen;
		for (const std::string& scene : m_RecentScenes)
		{
			std::error_code exists;
			if (std::filesystem::exists(scene, exists) && ImGui::MenuItem(std::filesystem::path(scene).filename().string().c_str()))
			{
				chosen = scene;
			}
		}
		ImGui::EndMenu();
		if (!chosen.empty())
		{
			open_scene(chosen);
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Save", "Ctrl+S"))
	{
		save_scene();
	}
	if (ImGui::MenuItem("Save As..."))
	{
		m_SaveAsRequested = true;
	}
	ImGui::EndDisabled();
}

void EditorModule::scene_popups()
{
	wiwa::AssetDatabase& database = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database();
	const std::filesystem::path& root = database.get_assets_root();
	// a scene is saved next to the one being edited, or in assets/scenes
	const std::filesystem::path folder = m_SceneEditor->path().empty() ? root / "scenes" : m_SceneEditor->path().parent_path();

	if (m_OpenSceneRequested)
	{
		m_OpenSceneRequested = false;
		m_SceneChoices.clear();
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
		{
			if (it->is_regular_file(error) && it->path().extension() == ".wscene")
			{
				m_SceneChoices.push_back(it->path().generic_string());
			}
		}
		std::sort(m_SceneChoices.begin(), m_SceneChoices.end());
		ImGui::OpenPopup("Open scene");
	}
	if (m_SaveAsRequested)
	{
		m_SaveAsRequested = false;
		m_SaveAsFocus = true;
		std::string name = m_SceneEditor->path().empty() ? "untitled" : m_SceneEditor->path().stem().string() + " copy";
		const std::string base = name;
		for (int number = 2; number < 1000 && !wiwa::validate_name(name, folder, wiwa::CreateKind::Scene).empty(); number++)
		{
			name = base + " " + std::to_string(number);
		}
		std::snprintf(m_SaveAsName, sizeof(m_SaveAsName), "%s", name.c_str());
		ImGui::OpenPopup("Save scene as");
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Open scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_SceneChoices.empty())
		{
			ImGui::TextDisabled("No scenes in the project");
		}
		else if (ImGui::BeginChild("##scene_list", ImVec2(420.0f, 220.0f), ImGuiChildFlags_Borders))
		{
			for (const std::string& scene : m_SceneChoices)
			{
				if (ImGui::Selectable(std::filesystem::path(scene).lexically_relative(root).generic_string().c_str()))
				{
					open_scene(scene);
					ImGui::CloseCurrentPopup();
				}
			}
		}
		if (!m_SceneChoices.empty())
		{
			ImGui::EndChild();
		}
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Save scene as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextDisabled("in %s", folder.generic_string().c_str());
		if (m_SaveAsFocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_SaveAsFocus = false;
		}
		const bool entered = ImGui::InputText("##save_as_name", m_SaveAsName, sizeof(m_SaveAsName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::SameLine();
		ImGui::TextDisabled(".wscene");

		const std::string reason = wiwa::validate_name(m_SaveAsName, folder, wiwa::CreateKind::Scene);
		if (!reason.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", reason.c_str());
		}

		ImGui::BeginDisabled(!reason.empty());
		const bool confirmed = ImGui::Button("Save") || (entered && reason.empty());
		ImGui::EndDisabled();
		if (confirmed)
		{
			const std::filesystem::path file = folder / (std::string(m_SaveAsName) + ".wscene");
			if (!m_SceneEditor->save_as(file))
			{
				show_scene_status("Could not save " + file.string() + " (see the log)");
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void EditorModule::export_button()
{
	wiwa::AssetPipelineModule* assets = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>();
	const Ref<wiwa::JobProgress> progress = assets->get_pack_progress();
	const bool packing = progress && !progress->finished();

	ImGui::BeginDisabled(m_PlayMode->state() != wiwa::PlayState::Edit || packing);
	if (ImGui::MenuItem("Export"))
	{
		assets->pack_async("../game/");
		m_ExportRequested = true;
		m_ExportUnsaved = m_SceneEditor->dirty();
	}
	ImGui::EndDisabled();
}

void EditorModule::export_modal()
{
	constexpr const char* popup = "Exporting";
	const Ref<wiwa::JobProgress> progress = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_pack_progress();
	const bool packing = progress && !progress->finished();

	// Escape closes a modal popup; while the pack runs it comes back
	if (m_ExportRequested || (m_ExportShown && packing && !ImGui::IsPopupOpen(popup)))
	{
		m_ExportRequested = false;
		m_ExportShown = true;
		ImGui::OpenPopup(popup);
	}
	else if (m_ExportShown && !packing && !ImGui::IsPopupOpen(popup))
	{
		m_ExportShown = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		if (packing)
		{
			const std::string stage = progress->stage();
			ImGui::Text("Exporting the game...");
			ImGui::ProgressBar(progress->fraction(), ImVec2(360.0f, 0.0f), stage.c_str());
		}
		else
		{
			ImGui::Text(progress && progress->succeeded() ? "Exported" : "The export failed (see the console)");
		}
		if (m_ExportUnsaved)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "The scene has unsaved changes: the export has the saved version.");
		}
		if (!packing)
		{
			if (ImGui::Button("Close"))
			{
				m_ExportShown = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
}

std::string EditorModule::run_play_smoke()
{
	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	Ref<wiwa::Scene> edit = m_SceneEditor->get_scene();

	const entt::entity probe = m_SceneEditor->create_entity(entt::null);
	const entt::entity other = m_SceneEditor->create_entity(entt::null);
	const wiwa::EntityGuid probeGuid = edit->registry().get<wiwa::Id>(probe).Guid;
	const wiwa::EntityGuid otherGuid = edit->registry().get<wiwa::Id>(other).Guid;
	const bool dirtyBefore = m_SceneEditor->dirty();
	const glm::vec3 positionBefore = edit->registry().get<wiwa::Transform>(probe).Position;

	m_SceneEditor->select(probe);
	m_PlaySmokeEdit = edit;
	m_PlaySmokeCount = edit->registry().storage<wiwa::Id>().size();

	m_PlayMode->play();
	if (m_PlayMode->state() != wiwa::PlayState::Playing)
	{
		return "play did not start";
	}
	if (m_SceneEditor->selected() == entt::null || m_PlayMode->play_scene()->registry().get<wiwa::Id>(m_SceneEditor->selected()).Guid != probeGuid)
	{
		return "the selection was lost when play started";
	}

	// a script asks for a scene to be added, and Stop comes before the load is done: it must not end up in the scene being edited
	scenes->load_async(std::string_view("assets/scenes/demo.wscene"), wiwa::LoadMode::Additive);

	Ref<wiwa::Scene> clone = m_PlayMode->play_scene();
	if (clone == nullptr || clone == edit || scenes->get_active() != clone || m_SceneEditor->get_scene() != clone || !m_SceneEditor->playing())
	{
		return "the clone is not the scene that is shown";
	}
	const entt::entity cloneProbe = clone->find(probeGuid);
	const entt::entity cloneOther = clone->find(otherGuid);
	if (cloneProbe == entt::null || cloneOther == entt::null)
	{
		return "the clone lacks the entities of the edit scene";
	}

	clone->registry().get<wiwa::Transform>(cloneProbe).Position = glm::vec3(9.0f);
	clone->destroy(cloneOther);

	m_PlayMode->pause();
	m_PlayMode->step();
	m_PlayMode->update();
	m_PlayMode->update();
	if (m_PlayMode->state() != wiwa::PlayState::Paused)
	{
		return "step left play mode";
	}
	m_PlayMode->resume();
	m_PlayMode->stop();

	if (m_PlayMode->state() != wiwa::PlayState::Edit || scenes->get_active() != edit || m_SceneEditor->get_scene() != edit || m_SceneEditor->playing())
	{
		return "the edit scene was not put back";
	}
	if (edit->registry().get<wiwa::Transform>(probe).Position != positionBefore || edit->find(otherGuid) == entt::null)
	{
		return "the edit scene was changed by playing";
	}
	if (m_SceneEditor->dirty() != dirtyBefore)
	{
		return "the dirty flag changed";
	}
	m_PlaySmokeFinalFrame = m_Frame + 90; // and later: nothing may have landed in the edit scene
	return {};
}

void EditorModule::set_theme()
{
	auto& colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

	// Headers
	colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
	colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
	colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

	// Buttons
	colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
	colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
	colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

	// Frame BG
	colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
	colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
	colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

	// Tabs
	colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
	colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
	colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

	// Title
	colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("engine/fonts/CascadiaCode.ttf", 16.0f);
	{
		const ImWchar icons_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.GlyphMinAdvanceX = 16.0f;
		io.Fonts->AddFontFromFileTTF("engine/fonts/forkawesome-webfont.ttf", 16.0f, &icons_config, icons_ranges);
	}
	io.Fonts->Build();
}
