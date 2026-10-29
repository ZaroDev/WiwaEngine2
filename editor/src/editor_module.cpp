#include "editor_module.h"

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>
#include <renderer/window.h>

#include <imgui.h>
#include <imgui_internal.h>

void EditorModule::init()
{
	const std::string editorTitle = fmt::format("Wiwa2 Editor - ({0}) <{1}>", "Default project", WI_BUILD_CONFIGURATION);
	wiwa::Engine::get().get_module<wiwa::Window>()->set_title(editorTitle);

	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	m_SceneEditor = CreateScope<wiwa::SceneEditor>(*wiwa::Engine::get().get_module<wiwa::AssetManager>(), *scenes);
	m_SceneEditor->set_scene(scenes->get_active());
	m_SceneEditor->set_follow_module(true);
	m_HierarchyPanel = CreateScope<HierarchyPanel>(*m_SceneEditor);
	m_InspectorPanel = CreateScope<InspectorPanel>(*m_SceneEditor);
	m_EnvironmentPanel = CreateScope<EnvironmentPanel>(*m_SceneEditor);
	m_LightingPanel = CreateScope<LightingPanel>(*m_SceneEditor);
}

void EditorModule::shutdown()
{
	m_LightingPanel.reset();
	m_EnvironmentPanel.reset();
	m_InspectorPanel.reset();
	m_HierarchyPanel.reset();
	m_SceneEditor.reset();
}

void EditorModule::pre_update(float deltaTime)
{
}

void EditorModule::update(float deltaTime)
{
	m_RendererPanel.draw();
	m_ViewportPanel.draw();
	m_SceneEditor->update();
	m_HierarchyPanel->draw();
	m_InspectorPanel->draw();
	m_EnvironmentPanel->draw();
	m_LightingPanel->draw();
	m_QualityPanel.draw();
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
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None | ImGuiDockNodeFlags_PassthruCentralNode;
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoBackground;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	static bool show = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &show, window_flags);

	ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);
	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}

	ImGui::End();
}

void EditorModule::main_bar()
{
	if (ImGui::BeginMainMenuBar())
	{
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
