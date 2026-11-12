#pragma once

#include <span>
#include <string>

#include "video_types.h"

namespace wiwa
{
	// What a platform gives the video system: a demuxer and decoder for one file held in memory. Pull based and called from one thread only (the
	// player's decode worker), so an implementation needs no locking. Media Foundation is the Windows one (video/mf), the Null decoder makes
	// synthetic streams for checks. A new platform is one new class and a case in create_platform_decoder().
	class IVideoDecoder
	{
	public:
		virtual ~IVideoDecoder() = default;

		// Opens the file bytes (they outlive the decoder). False with a reason in error: not a video, no video stream, a codec the platform
		// cannot read.
		virtual bool open(std::span<const std::byte> bytes, VideoInfo& info, std::string& error) = 0;

		// The next picture in presentation order, or why there is none.
		virtual DecodeStatus read_video(VideoFrame& out) = 0;
		// The next run of sound. Only valid when the file has audio.
		virtual DecodeStatus read_audio(AudioChunk& out) = 0;

		// Moves both streams to the first sample at or after the time. False if the file cannot seek.
		virtual bool seek(i64 us) = 0;
		virtual void close() = 0;
	};
}
