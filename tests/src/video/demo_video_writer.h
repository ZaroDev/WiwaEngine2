#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_tests --write-demo-video: writes the demo scene's clip, editor/assets/video/Intro.mp4 (H.264 and AAC, colour bars, a moving square, a 440 Hz
	// tone and then a 660 Hz tone), with Media Foundation. A tool, not a check: run it again when the clip should change; the editor re-imports it.
	// Returns true if the file was written.
	bool run_write_demo_video(AssetPipelineModule& pipeline);
}
