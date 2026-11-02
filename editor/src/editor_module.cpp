#include "editor_module.h"

#include <asset_pipeline/asset_pipeline_module.h>
#include <asset_pipeline/asset_templates.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>
#include <renderer/window.h>
#include <scripting/script_module.h>

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
		virtual void start_scripts() override { m_Scripts.start(); }
		virtual void stop_scripts() override { m_Scripts.stop(); }
		virtual void cancel_loads() override { m_Scenes.cancel_loads(); }
		virtual void time_play() override { wiwa::Engine::get().get_time().play_game(); }
		virtual void time_stop() override { wiwa::Engine::get().get_time().stop_game(); }
		virtual void time_pause(bool paused) override { wiwa::Engine::get().get_time().set_game_pause(paused); }

	private:
		wiwa::SceneEditor& m_Editor;
		wiwa::SceneModule& m_Scenes;
		wiwa::ScriptingModule& m_Scripts;
	};
}

void EditorModule::init()
{
	const std::string editorTitle = fmt::format("Wiwa2 Editor - ({0}) <{1}>", "Default project", WI_BUILD_CONFIGURATION);
	wiwa::Engine::get().get_module<wiwa::Window>()->set_title(editorTitle);

	// scripts run in play mode only; editing a scene does not run them
	wiwa::Engine::get().get_module<wiwa::ScriptingModule>()->set_auto_start(false);
	wiwa::Engine::get().get_time().stop_game(); // and there is no game time either

	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	m_SceneEditor = CreateScope<wiwa::SceneEditor>(*wiwa::Engine::get().get_module<wiwa::AssetManager>(), *scenes);
	m_SceneEditor->set_scene(scenes->get_active());
	m_SceneEditor->set_follow_module(true);

	m_PlayHost = CreateScope<EditorPlayHost>(*m_SceneEditor, *scenes, *wiwa::Engine::get().get_module<wiwa::ScriptingModule>());
	m_PlayMode = CreateScope<wiwa::PlayMode>(*m_PlayHost);
	for (const std::string& argument : wiwa::Engine::get().get_arguments())
	{
		m_PlaySmoke = m_PlaySmoke || argument == "--play-smoke";
		if (argument == "--demo-smoke")
		{
			m_DemoSmoke = CreateScope<wiwa::DemoSmoke>(*m_SceneEditor, *m_PlayMode);
		}
	}

	m_Panels.push_back(CreateRef<HierarchyPanel>(*m_SceneEditor, wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database()));
	Ref<InspectorPanel> inspector = CreateRef<InspectorPanel>(*m_SceneEditor);
	m_Panels.push_back(inspector);
	m_Panels.push_back(CreateRef<WorldPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<ViewportPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<RendererPanel>());
	m_Panels.push_back(CreateRef<ConsolePanel>());
	Ref<MaterialPanel> materialPanel = CreateRef<MaterialPanel>();
	m_Panels.push_back(materialPanel);
	Ref<AssetBrowserPanel> browser = CreateRef<AssetBrowserPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());
	m_Panels.push_back(browser);
	// the panels live as long as m_Panels, which holds all of them until shutdown
	MaterialPanel* materials = materialPanel.get();
	AssetBrowserPanel* browserPanel = browser.get();
	browser->set_open_material([materials](const std::filesystem::path& wmat) { materials->open(wmat); });
	wiwa::SceneEditor* sceneEditor = m_SceneEditor.get();
	browser->set_make_prefab([sceneEditor](entt::entity entity, const std::filesystem::path& folder) { return sceneEditor->make_prefab(entity, folder); });
	inspector->set_locate([browserPanel](const wiwa::AssetRef& ref) { browserPanel->select_asset(ref); });
	m_BuildSettings = CreateScope<BuildSettingsPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database());

	set_theme();
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

	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		save_scene();
	}
	main_bar();
	export_modal();
	scene_popups();
	m_BuildSettings->on_draw();
	dock_space();

	// Adopts the scene the SceneModule loaded (or opened), and expands prefab instances once their prefab is there.
	m_SceneEditor->update();
	if (const std::string message = m_SceneEditor->take_message(); !message.empty())
	{
		show_scene_status(message);
	}

	for (auto& panel : m_Panels)
	{
		panel->on_update();
	}

	for (auto& panel : m_Panels)
	{
		if (panel->is_active())
		{
			panel->on_draw();
		}
	}
}

void EditorModule::post_update(float deltaTime)
{
	// after the scripts ran: a step ends here, and a scene a script loaded becomes the one that plays
	m_PlayMode->update();
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
		ImGui::SetNextWindowSize(viewport->Size);
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
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
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
		ImGui::EndMainMenuBar();
	}
	ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	float height = ImGui::GetFrameHeight();

	// the bar is tinted while the scene plays, so that nobody edits the copy thinking it is the scene
	const bool playing = m_PlayMode->state() != wiwa::PlayState::Edit;
	if (playing)
	{
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.55f, 0.28f, 0.05f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{ 0.55f, 0.28f, 0.05f, 1.0f });
	}

	if (ImGui::BeginViewportSideBar("##SecondaryMenuBar", viewport, ImGuiDir_Up, height, window_flags))
	{
		if (ImGui::BeginMenuBar())
		{
			play_toolbar();
			ImGui::EndMenuBar();
		}
		ImGui::End();
	}

	if (playing)
	{
		ImGui::PopStyleColor(2);
	}
}

void EditorModule::play_toolbar()
{
	const wiwa::PlayState state = m_PlayMode->state();

	if (state == wiwa::PlayState::Edit)
	{
		ImGui::BeginDisabled(!m_PlayHost->can_play());
		if (ImGui::Button("Play"))
		{
			m_PlayMode->play();
		}
		ImGui::EndDisabled();
	}
	else if (ImGui::Button("Stop"))
	{
		m_PlayMode->stop();
	}

	ImGui::BeginDisabled(state == wiwa::PlayState::Edit);
	if (ImGui::Button(state == wiwa::PlayState::Paused ? "Resume" : "Pause"))
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
	if (ImGui::Button("Step"))
	{
		m_PlayMode->step();
	}
	ImGui::EndDisabled();

	ImGui::TextDisabled("|");
	export_button();

	if (!m_SceneStatus.empty() && ImGui::GetTime() < m_SceneStatusUntil)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "  %s", m_SceneStatus.c_str());
	}

	if (state != wiwa::PlayState::Edit)
	{
		ImGui::TextUnformatted(state == wiwa::PlayState::Paused ? "  Paused (changes to this scene are lost on Stop)" : "  Playing (changes to this scene are lost on Stop)");
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
					m_SceneEditor->open(scene);
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
	if (ImGui::Button("Export"))
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
			ImGui::TextUnformatted("Exporting the game...");
			ImGui::ProgressBar(progress->fraction(), ImVec2(360.0f, 0.0f), stage.c_str());
		}
		else
		{
			ImGui::TextUnformatted(progress && progress->succeeded() ? "Exported" : "The export failed (see the console)");
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
