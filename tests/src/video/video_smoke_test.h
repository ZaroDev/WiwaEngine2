#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_tests --video-smoke: checks the video system on the Null decoder, with no GPU and no sound card: the decoder interface, the clock and the
	// frame choice, the player (states, sync, seek, loop, failures, shutdown), the audio feed and the module. Returns true if everything held
	// (and logs "Video smoke test passed").
	bool run_video_smoke_test(AssetPipelineModule& pipeline);
}
