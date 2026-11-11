#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --nav-smoke: checks the navmesh asset, the components and settings, the Recast bake, the Detour queries, the agents of the
	// NavModule and the Lua API. Returns true if everything held (and logs "Nav smoke test passed").
	bool run_nav_smoke_test(AssetPipelineModule& pipeline);
}
