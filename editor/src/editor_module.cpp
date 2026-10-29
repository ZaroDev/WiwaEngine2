#include "editor_module.h"

#include <asset_pipeline/asset_pipeline_module.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>
#include <renderer/window.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>

void EditorModule::init()
{
	const std::string editorTitle = fmt::format("Wiwa2 Editor - ({0}) <{1}>", "Default project", WI_BUILD_CONFIGURATION);
	wiwa::Engine::get().get_module<wiwa::Window>()->set_title(editorTitle);

	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	m_SceneEditor = CreateScope<wiwa::SceneEditor>(*wiwa::Engine::get().get_module<wiwa::AssetManager>(), *scenes);
	m_SceneEditor->set_scene(scenes->get_active());
	m_SceneEditor->set_follow_module(true);

	m_Panels.push_back(CreateRef<HierarchyPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<InspectorPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<EnvironmentPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<LightingPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<ExposurePanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<BloomPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<TonemapPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<AoPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<ViewportPanel>(*m_SceneEditor));
	m_Panels.push_back(CreateRef<RendererPanel>());
	m_Panels.push_back(CreateRef<QualityPanel>());
	m_Panels.push_back(CreateRef<ConsolePanel>());
	m_Panels.push_back(CreateRef<AssetBrowserPanel>(wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database()));

	set_theme();
}

void EditorModule::shutdown()
{
	
}

void EditorModule::pre_update(float deltaTime)
{
}

void EditorModule::update(float deltaTime)
{
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
		if (ImGui::BeginMenu("Window"))
		{
			for (auto& panel : m_Panels)
			{
				if (panel->get_category() == PanelCategory::Window)
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
				if (panel->get_category() == PanelCategory::Info)
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
				if (panel->get_category() == PanelCategory::Renderer)
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

	if (ImGui::BeginViewportSideBar("##SecondaryMenuBar", viewport, ImGuiDir_Up, height, window_flags))
	{
		if (ImGui::BeginMenuBar())
		{
			ImGui::EndMenuBar();
		}
		ImGui::End();
	}
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

	/*ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("engine/fonts/CascadiaCode.ttf", 16.0f);
	{
		const ImWchar icons_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.GlyphMinAdvanceX = 16.0f;
		io.Fonts->AddFontFromFileTTF("engine/fonts/forkawesome-webfont.ttf", 16.0f, &icons_config, icons_ranges);
	}
	io.Fonts->Build();*/
}
