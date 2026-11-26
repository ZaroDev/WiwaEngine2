#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

#include <functional>

class EditorModule;

namespace wiwa
{
	// wiwa_tests --new-project-smoke: a project made by the launcher's create_project (the real Minimal template, in a scratch folder) opened by an
	// editor for the first time gets its starter scene. The editor run is a child process (`wiwa_tests --project <scratch> --starter-scene-child`):
	// this process works in editor/ and must not write its scenes or build settings. Returns true if everything held.
	bool run_new_project_smoke_test(AssetPipelineModule& pipeline);

	// The child's per-frame check (--starter-scene-child): quits with 0 once the editor's starter scene step is Done, 1 when it Failed, was not
	// needed or did not finish in time.
	void starter_scene_child_frame(EditorModule& editor);
}
