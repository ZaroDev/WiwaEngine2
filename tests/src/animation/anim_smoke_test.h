#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_editor --anim-smoke: checks the animation foundation (animation.md): the import of skeletons and clips, pose math and
	// nodes, the Animator module, the Lua API and the edit model.
	// Returns true if everything held (and logs "Animation smoke test passed").
	bool run_anim_smoke_test(AssetPipelineModule& pipeline);
}
