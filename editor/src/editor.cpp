#pragma once
#include <editor_common.h>
#include <core/engine.h>

#include "editor_module.h"
#include "asset_pipeline/asset_pipeline_module.h"
#include "asset_pipeline/asset_smoke_test.h"
#include "asset_pipeline/packager.h"

class Editor final : public wiwa::Engine
{
public:
	Editor(int argc, char** argv) : wiwa::Engine(argc, argv)
	{
		for (int i = 1; i < argc; i++)
		{
			m_Args.emplace_back(argv[i]);
		}
		
	}

	

	virtual ~Editor() override
	{

	}

protected:
	std::vector<std::string> m_Args{};

	virtual void on_init() override
	{
		wiwa::AssetPipelineModule* assets = m_ModuleManager.add_module<wiwa::AssetPipelineModule>();
		m_ModuleManager.add_module<EditorModule>();

		// Command-line runs: do the job, set the exit code, and leave without opening the editor loop.
		for (size_t i = 0; i < m_Args.size(); i++)
		{
			if (m_Args[i] == "--pack")
			{
				bool ok = false;
				if (i + 1 < m_Args.size())
				{
					Ref<wiwa::JobProgress> progress = assets->pack_async(m_Args[i + 1]);
					progress->wait();
					ok = progress->succeeded();
				}
				if (i + 1 >= m_Args.size())
				{
					WI_ERROR("Assets", "--pack needs an output folder");
				}
				set_exit_code(ok ? 0 : 1);
				request_quit();
			}
			else if (m_Args[i] == "--asset-smoke")
			{
				set_exit_code(wiwa::run_asset_smoke_test(*assets) ? 0 : 1);
				request_quit();
			}
		}
	}
};

wiwa::Engine* wiwa::create_engine(int argc, char** argv)
{
	return new Editor(argc, argv);
}