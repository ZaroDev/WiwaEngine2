#pragma once

#include <string>
#include <vector>

#include <math/math.h>

// Plain data of the video system: what a decoder says about a file and the pictures and sound it gives. No platform type is visible here.
namespace wiwa
{
	// What the decoder found in a file.
	struct VideoInfo
	{
		u32 Width = 0;
		u32 Height = 0;
		float Fps = 0.0f;
		double DurationSeconds = 0.0;
		bool HasAudio = false;
		u32 SampleRate = 0;
		u32 Channels = 0;
	};

	// One decoded picture: top-down BGRA8, rows tightly packed (Bgra.size() == Width * Height * 4).
	struct VideoFrame
	{
		u32 Width = 0;
		u32 Height = 0;
		i64 TimestampUs = 0;
		std::vector<u8> Bgra{};
	};

	// A run of decoded sound: interleaved 32 bit float samples, Samples.size() == Frames * channels.
	struct AudioChunk
	{
		i64 TimestampUs = 0;
		u32 Frames = 0;
		std::vector<float> Samples{};
	};

	enum class DecodeStatus : u32
	{
		Ok,      // the output was filled
		Pending, // nothing yet (the decoder is behind); ask again
		End,     // the stream is over
		Error,   // the stream is broken; the decoder stays unusable until seek or close
	};

	constexpr u32 VIDEO_MAX_DIMENSION = 8192; // a picture larger than this on either side is refused
}
