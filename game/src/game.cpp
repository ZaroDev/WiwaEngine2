#include <core/engine.h>
#include <renderer/render_module.h>
#include <scene/scene_module.h>

// A game on the engine. It has no editor in it: scenes and assets come from the pack (assets.wpak) next to the program.
//
//   wiwa_game                                        load the default scene of the pack (chosen in the editor's Build Settings)
//   wiwa_game --scene Assets/scenes/level.wscene     load that scene instead (it must be in the pack)
//   wiwa_game --scene <path> --scene-smoke-load      ...and quit when it is loaded: exit code 0 if it worked, 1 if not
//   wiwa_game --debug-draw-demo                      draw the debug renderer's shapes over the scene every frame (axes, a grid, a box)
class Game final : public wiwa::Engine
{
public:
	Game(int argc, char** argv) : wiwa::Engine(argc, argv) {}

protected:
	virtual void on_init() override
	{
		for (const std::string& argument : get_arguments())
		{
			m_QuitWhenLoaded = m_QuitWhenLoaded || argument == "--scene-smoke-load";
			m_DebugDrawDemo = m_DebugDrawDemo || argument == "--debug-draw-demo";
		}
	}

	virtual void on_update(float deltaTime) override
	{
		if (m_DebugDrawDemo)
		{
			// Immediate mode: shapes last one frame, so they are drawn again every frame. Works in every configuration.
			wiwa::DebugDraw& draw = get_module<wiwa::RenderModule>()->get_debug_draw();
			draw.grid(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 10, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), wiwa::DebugDepth::Faded);
			draw.axes(glm::mat4(1.0f), 2.0f, wiwa::DebugDepth::AlwaysOnTop, 3.0f);
			draw.box(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec4(0.3f, 1.0f, 0.3f, 1.0f), wiwa::DebugDepth::Faded, 2.0f);
		}

		if (!m_QuitWhenLoaded)
		{
			return;
		}

		const Ref<wiwa::JobProgress> load = get_module<wiwa::SceneModule>()->get_startup_load();
		if (load == nullptr)
		{
			set_exit_code(1); // asked to quit after loading, but there is no --scene
			request_quit();
		}
		else if (load->finished())
		{
			set_exit_code(load->succeeded() ? 0 : 1);
			request_quit();
		}
	}

private:
	bool m_QuitWhenLoaded = false;
	bool m_DebugDrawDemo = false;
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Game(argc, argv);
}
