#pragma once
#include <editor_common.h>
#include <assets/asset_manager.h>
#include <core/engine.h>

#include "editor_module.h"
#include "gizmo/gizmo_settings.h"
#include "asset_pipeline/asset_pipeline_module.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/asset_smoke_test.h"
#include "asset_pipeline/packager.h"
#include "scene/scene_smoke_test.h"
#include "scripting/script_smoke_test.h"
#include "ui/ui_smoke_test.h"
#include "physics/physics_smoke_test.h"
#include "input/input_smoke_test.h"
#include "audio/audio_smoke_test.h"
#include "animation/anim_smoke_test.h"

class Editor final : public wiwa::Engine
{
public:
	Editor(int argc, char** argv) : wiwa::Engine(argc, argv)
	{
		// The engine's own assets (shaders, ...) are cooked before the engine starts: on a cold Library (the first run, or after the cooked format
		// changed) the renderer would find none and a pass would fail. A warm Library only checks that they are up to date.
		wiwa::Log::init();
		{
			wiwa::AssetDatabase database("assets", "library");
			database.add_root("engine", "library/engine");
			wiwa::register_builtin_importers(database);
			database.cook_root(1);
		}

		// The editor reads the Library it imports into, never a pack.
		wiwa::AssetManager::set_prefer_pack(false);

		for (int i = 1; i < argc; i++)
		{
			m_Args.emplace_back(argv[i]);
		}

		// Command-line runs (smokes, --pack) keep the plain window; an interactive start follows the "Start maximized" setting.
		const bool interactive = std::none_of(m_Args.begin(), m_Args.end(), [](const std::string& arg) { return arg.rfind("--", 0) == 0; });
		set_start_maximized(interactive && wiwa::GizmoSettings::load().StartMaximized);
	}

	

	virtual ~Editor() override
	{
		if (m_CommandThread.joinable())
		{
			m_CommandThread.join();
		}
	}

protected:
	std::vector<std::string> m_Args{};
	std::thread m_CommandThread{};
	std::atomic<int> m_CommandResult{ -1 }; // -1 while a command-line job runs, else its exit code

	virtual void on_init() override
	{
		wiwa::AssetPipelineModule* assets = m_ModuleManager.add_module<wiwa::AssetPipelineModule>();
		m_ModuleManager.add_module<EditorModule>();

		// Command-line runs (--pack <folder>, --asset-smoke): the work runs on its own thread and the editor loop keeps
		// going, so the window stays responsive; on_update quits with the result once the work is done.
		for (size_t i = 0; i < m_Args.size(); i++)
		{
			if (m_Args[i] == "--pack")
			{
				if (i + 1 >= m_Args.size())
				{
					WI_ERROR("Assets", "--pack needs an output folder");
					set_exit_code(1);
					request_quit();
					continue;
				}

				const std::filesystem::path outDirectory = m_Args[i + 1];
				m_CommandThread = std::thread([this, assets, outDirectory]()
				{
					Ref<wiwa::JobProgress> progress = assets->pack_async(outDirectory);
					progress->wait();
					m_CommandResult = progress->succeeded() ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--scene-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_scene_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--script-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_script_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--ui-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_ui_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--physics-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_physics_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--input-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_input_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--audio-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_audio_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--anim-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_anim_smoke_test(*assets) ? 0 : 1;
				});
			}
			else if (m_Args[i] == "--asset-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_asset_smoke_test(*assets) ? 0 : 1;
				});
			}
		}
	}

	virtual void on_update(float deltaTime) override
	{
		const int result = m_CommandResult;
		if (result >= 0)
		{
			set_exit_code(result);
			request_quit();
		}
	}
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Editor(argc, argv);
}