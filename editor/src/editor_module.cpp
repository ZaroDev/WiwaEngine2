#include "editor_module.h"

#include <core/engine.h>
#include <renderer/window.h>

void EditorModule::init()
{
	const std::string editorTitle = fmt::format("Wiwa2 Editor - ({0}) <{1}>", "Default project", WI_BUILD_CONFIGURATION);
	wiwa::Engine::get().get_module<wiwa::Window>()->set_title(editorTitle);
}

void EditorModule::shutdown()
{
}

void EditorModule::pre_update(float deltaTime)
{
}

void EditorModule::update(float deltaTime)
{
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
