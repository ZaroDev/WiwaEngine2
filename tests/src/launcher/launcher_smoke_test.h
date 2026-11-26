#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_tests --launcher-smoke: the launcher's ImGui-free parts (launcher/src/core): the recent projects list, what a project folder is, creating a
	// project from a template, finding the editor and the command line it gets. Works in a scratch folder under the temp directory; never touches
	// the real %APPDATA%\Wiwa\projects.json. Returns true if everything held (and logs "Launcher smoke test passed").
	bool run_launcher_smoke_test(AssetPipelineModule& pipeline);
}
