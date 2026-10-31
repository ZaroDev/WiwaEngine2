#include "editor_module.h"

#include <asset_pipeline/asset_pipeline_module.h>
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

	m_Panels.push_back(CreateRef<HierarchyPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<InspectorPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<EnvironmentPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<LightingPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<ExposurePanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<BloomPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<TonemapPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<AoPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<PostEffectsPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<ViewportPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<RendererPanel>());
	m_Panels.push_back(CreateRef<QualityPanel>());
	m_Panels.push_back(CreateRef<ConsolePanel>());
	m_Panels.push_back(CreateRef<AssetBrowserPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database()));
	m_Panels.push_back(CreateRef<BuildSettingsPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database()));

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

	main_bar();
	dock_space();

	// Adopts the scene the SceneModule loaded (or opened), and expands prefab instances once their prefab is there.
	m_SceneEditor->update();

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
			for (auto& panel : m_Panels)
			{
				if (any(panel->get_category(), PanelCategory::File))
				{
					if (ImGui::MenuItem(panel->get_name(), nullptr, panel->is_active()))
					{
						panel->switch_active();
					}
				}
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

	if (state != wiwa::PlayState::Edit)
	{
		ImGui::TextUnformatted(state == wiwa::PlayState::Paused ? "  Paused (changes to this scene are lost on Stop)" : "  Playing (changes to this scene are lost on Stop)");
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
