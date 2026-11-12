#pragma once

#include <filesystem>
#include <string>

#include <math/math.h>

namespace wiwa
{
	// What write_mf_test_clip makes: an animated picture (colour bars that scroll under a white square that moves across) and stereo sound that is one
	// steady tone, FirstHz for the first half of the clip and SecondHz for the second.
	struct MfClipConfig
	{
		u32 Width = 640;   // even
		u32 Height = 360;  // even
		u32 Fps = 30;
		double Seconds = 3.0;
		u32 SampleRate = 48000; // the AAC encoder takes 44100 and 48000
		float FirstHz = 440.0f;
		float SecondHz = 660.0f;
	};

	// Writes an mp4 (H.264 and AAC) with Media Foundation's sink writer. For the checks that need a clip whose content is known, and for the demo's
	// clip (`wiwa_tests --write-demo-video`): nothing at run time uses it. False with a reason on failure.
	bool write_mf_test_clip(const std::filesystem::path& file, const MfClipConfig& config, std::string& error);
}
