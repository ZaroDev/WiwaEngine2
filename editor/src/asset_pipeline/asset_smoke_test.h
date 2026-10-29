#pragma once

#include "asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --asset-smoke: imports Assets/, packs Library/, loads every asset back through the pack and through the
	// AssetManager, checks that damaged packs are rejected without a crash, and that re-importing keeps every guid.
	// Returns true if everything held (and logs "Asset smoke test passed").
	bool run_asset_smoke_test(AssetPipelineModule& pipeline);
}
