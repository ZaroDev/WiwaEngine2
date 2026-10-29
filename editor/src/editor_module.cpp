#include "editor_module.h"

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>
#include <renderer/window.h>

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
}

void EditorModule::shutdown()
{
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
	m_SceneEditor->update();
	m_HierarchyPanel->draw();
	m_InspectorPanel->draw();
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
