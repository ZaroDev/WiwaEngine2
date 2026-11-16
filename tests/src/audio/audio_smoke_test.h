#pragma once

#include <asset_pipeline/asset_pipeline_module.h>

namespace wiwa
{
	// wiwa_editor --audio-smoke: checks the audio module on the Null backend (its call log stands in for a vendor), the soundbank asset and
	// its metadata, the components and their play-mode sync, the settings, the Lua API and, when built with WI_AUDIO_WWISE, the Wwise backend.
	// Returns true if everything held (and logs "Audio smoke test passed").
	bool run_audio_smoke_test(AssetPipelineModule& pipeline);
}
