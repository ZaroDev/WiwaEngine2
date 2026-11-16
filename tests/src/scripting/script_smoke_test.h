#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_editor --script-smoke: checks the scripting runtime (asset type, importer, class loading, require, lifecycle, bindings).
	// Returns true if everything held (and logs "Script smoke test passed").
	bool run_script_smoke_test(AssetPipelineModule& pipeline);
}
