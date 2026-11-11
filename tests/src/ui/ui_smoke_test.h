#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_editor --ui-smoke: checks the game UI (panel data and files, fonts, layout, draw lists, interaction, the module and the
	// editor's document model) without needing a window. Returns true if everything held (and logs "UI smoke test passed").
	bool run_ui_smoke_test(AssetPipelineModule& pipeline);

	// wiwa_editor --ui-editor-smoke: the file for the run. Writes a panel with every kind of widget (one of them anchored to an entity) to a temporary
	// folder and returns its path; the editor opens it in the UI Editor panel, runs for a while with ImGui's checks on, and fails if the panel did
	// not load or is not previewing it.
	std::filesystem::path write_ui_editor_smoke_panel();
}
