#pragma once

#include "../asset_pipeline/asset_pipeline_module.h"

namespace wiwa
{
	// wiwa_editor --ui-smoke: checks the game UI (panel data and files, fonts, layout, draw lists, interaction, the module and the
	// editor's document model) without needing a window. Returns true if everything held (and logs "UI smoke test passed").
	bool run_ui_smoke_test(AssetPipelineModule& pipeline);
}
