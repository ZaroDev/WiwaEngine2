#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --gizmo-smoke: drives the transform gizmo, the viewport selection helpers and the scene gizmos with synthetic mouse
	// input and a camera, on a scene in memory (no window, no GPU). Returns true if everything held (and logs "Gizmo smoke test passed").
	bool run_gizmo_smoke_test(AssetPipelineModule& pipeline);
}
