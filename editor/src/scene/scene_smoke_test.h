#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --scene-smoke: checks the scene foundation (components, reflection, scene files, prefab instances and
	// overrides, async loading) without a window being involved. Returns true if everything held (and logs
	// "Scene smoke test passed").
	bool run_scene_smoke_test(AssetPipelineModule& pipeline);
}
