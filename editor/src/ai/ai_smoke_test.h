#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --ai-smoke: checks the behaviour tree asset and its validator, the runtime (nodes, blackboard, MoveTo, controllers), the Lua leaves and
	// the AI API, perception, and the tree edit model. Returns true if everything held (and logs "AI smoke test passed").
	bool run_ai_smoke_test(AssetPipelineModule& pipeline);
}
