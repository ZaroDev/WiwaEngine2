#pragma once
#include <editor_common.h>
#include <assets/asset_manager.h>
#include <core/engine.h>

#include "editor_module.h"
#include "gizmo/gizmo_settings.h"
#include "asset_pipeline/asset_pipeline_module.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/packager.h"

// The engine of the editor: cooks the engine assets, starts the asset pipeline and the editor, and runs the command-line jobs on a thread.
// wiwa_tests derives from it to add its smokes.
class Editor : public wiwa::Engine
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
		// --project <dir> only says where to work (main already entered it): it does not make the run a command-line one.
		bool interactive = true;
		for (size_t i = 0; i < m_Args.size(); i++)
		{
			if (m_Args[i] == "--project")
			{
				i++; // its value
			}
			else if (m_Args[i].rfind("--project=", 0) != 0 && m_Args[i].rfind("--", 0) == 0)
			{
				interactive = false;
			}
		}
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
	// What a derived engine changes: the editor module it starts, and the command-line arguments it knows besides --pack (which may start m_CommandThread).
	virtual void add_editor_module() { m_ModuleManager.add_module<EditorModule>(); }
	virtual void on_command_argument(const std::string& argument, wiwa::AssetPipelineModule* assets) {}

	std::vector<std::string> m_Args{};
	std::thread m_CommandThread{};
	std::atomic<int> m_CommandResult{ -1 }; // -1 while a command-line job runs, else its exit code

	virtual void on_init() override
	{
		wiwa::AssetPipelineModule* assets = m_ModuleManager.add_module<wiwa::AssetPipelineModule>();
		add_editor_module();

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
			else
			{
				on_command_argument(m_Args[i], assets);
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
