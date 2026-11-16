#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_tests --video-mf-smoke: checks the platform decoder (Media Foundation on Windows) on the demo clip editor/assets/video/Intro.mp4: the stream
	// info, the pictures and sound it gives, seek, the end of the stream, a damaged and a foreign file, the importer's probe, and a VideoPlayer
	// playing the clip on the decode worker. Without the clip it logs "skipped" and passes (read the log: that is not a check of the decoder).
	// Returns true if everything held (and logs "Video MF smoke test passed").
	bool run_video_mf_smoke_test(AssetPipelineModule& pipeline);
}
