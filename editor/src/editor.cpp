#pragma once
#include <editor_common.h>
#include <assets/asset_manager.h>
#include <core/engine.h>

#include "editor_module.h"
#include "asset_pipeline/asset_pipeline_module.h"
#include "asset_pipeline/asset_smoke_test.h"
#include "asset_pipeline/packager.h"
#include "gizmo/gizmo_smoke_test.h"
#include "scene/scene_smoke_test.h"

class Editor final : public wiwa::Engine
{
public:
	Editor(int argc, char** argv) : wiwa::Engine(argc, argv)
	{
		// The editor reads the Library it imports into, never a pack.
		wiwa::AssetManager::set_prefer_pack(false);

		for (int i = 1; i < argc; i++)
		{
			m_Args.emplace_back(argv[i]);
		}
		
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
			else if (m_Args[i] == "--gizmo-smoke")
			{
				m_CommandThread = std::thread([this, assets]()
				{
					m_CommandResult = wiwa::run_gizmo_smoke_test(*assets) ? 0 : 1;
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