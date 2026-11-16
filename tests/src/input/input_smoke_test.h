#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_editor --input-smoke: checks the input module. Raw layer so far: key, mouse and gamepad edges (a tap shorter than a frame,
	// focus loss, an unplugged pad), the capture gate and the control names.
	// Returns true if everything held (and logs "Input smoke test passed").
	bool run_input_smoke_test(AssetPipelineModule& pipeline);
}
