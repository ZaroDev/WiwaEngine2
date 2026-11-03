#include "game_module.h"
#include "imgui.h"
#include <renderer/render_module.h>
#include <core/engine.h>
#include <input/input_module.h>
#include <ui/ui_module.h>
#include <time/time.h>

#define WI_GAME_DEBUG_STATS WI_DEBUG || WI_RELEASE

void GameModule::init()
{
	// the game UI reads the mouse, keys and gamepads through the input module
	wiwa::UiModule* ui = wiwa::Engine::get().get_module<wiwa::UiModule>();
	wiwa::InputModule* input = wiwa::Engine::get().get_module<wiwa::InputModule>();
	if (ui != nullptr && input != nullptr)
	{
		ui->set_input_source(&input->ui_source());
	}
}

void GameModule::shutdown()
{
}

void GameModule::update(float deltaTime)
{
#if WI_GAME_DEBUG_STATS
	draw_debug_stats();
#endif
}

void GameModule::draw_debug_stats()
{
	static bool drawStats = false;
	if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
	{
		drawStats = !drawStats;
	}

	if (drawStats)
	{
		wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
		if (render == nullptr || !render->is_ready())
		{
			return;
		}

		const wiwa::RenderSnapshot snapshot = render->get_snapshot();

		const wiwa::Time& time = wiwa::Engine::get().get_time();

		ImGui::Begin("Stats");
		ImGui::Text("Render thread %.1f FPS (%.2f ms)", snapshot.Fps, snapshot.FrameMs);
		ImGui::Text("Main thread %.1f FPS (%.2f ms)", 1.f / time.get_delta_time_seconds(), time.get_delta_time());
		bool vsync = snapshot.Vsync;
		if (ImGui::Checkbox("Vertical sync", &vsync))
		{
			render->set_vsync(vsync);
		}

		ImGui::End();
	}
}
