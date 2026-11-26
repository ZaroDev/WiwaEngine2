#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_tests --project-dir-smoke: the --project argument (core/project_dir.h): parsing, finding the engine's assets, the directory link a project
	// gets to them. Does not change the working directory of the running process (other threads read relative paths); the real switch is covered by
	// running any smoke with `--project <folder>` (project_dir.md of project-settings.md).
	// Returns true if everything held (and logs "Project dir smoke test passed").
	bool run_project_dir_smoke_test(AssetPipelineModule& pipeline);
}
