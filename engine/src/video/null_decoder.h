#pragma once

#include <core/common.h>

#include "video_decoder.h"

namespace wiwa
{
	// What the Null decoder makes. It ignores the bytes it is given.
	struct NullDecoderConfig
	{
		u32 Width = 64;
		u32 Height = 36;
		float Fps = 30.0f;
		double DurationSeconds = 2.0;
		double AudioDurationSeconds = -1.0; // the length of the sound when it is not the length of the picture (-1: the same)
		bool HasAudio = true;
		u32 SampleRate = 48000;
		u32 Channels = 2;

		bool FailOpen = false;       // open() fails
		u32 OpenDelayMs = 0;         // open() takes this long (a file that is slow to open)
		i64 AudioSeekBackUs = 0;     // after a seek the sound restarts this much before the time (a decoder that seeks to a keyframe)
		i64 FailAfterFrames = -1;    // read_video gives Error once this many frames were read (-1: never)
		u32 PendingEvery = 0;        // every Nth read_video call gives Pending without advancing (0: never)
	};

	// A synthetic stream for checks, with no file and no platform API. Frame n has the timestamp n / Fps and its first four BGRA bytes hold n
	// (little endian, the rest is a gradient); the sound is a 440 Hz sine, continuous across chunks and seeks.
	Scope<IVideoDecoder> create_null_decoder(const NullDecoderConfig& config);
}
