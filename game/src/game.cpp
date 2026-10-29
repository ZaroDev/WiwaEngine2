#include <core/engine.h>
#include <scene/scene_module.h>

// A game on the engine. It has no editor in it: scenes and assets come from the pack (assets.wpak) next to the program.
//
//   wiwa_game --scene Assets/scenes/level.wscene     load that scene when the game starts
//   wiwa_game --scene <path> --scene-smoke-load      ...and quit when it is loaded: exit code 0 if it worked, 1 if not
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
		}
	}

	virtual void on_update(float deltaTime) override
	{
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
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Game(argc, argv);
}
