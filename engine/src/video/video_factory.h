#pragma once

#include <span>
#include <string>

#include <core/common.h>

#include "video_decoder.h"

namespace wiwa
{
	// The decoder this platform has (Media Foundation on Windows), or null where there is none. Adding a platform is a decoder class and a case here.
	Scope<IVideoDecoder> create_platform_decoder();

	// Opens the bytes with the platform decoder and closes them again: what the importer records about a file. False with a reason when there is no
	// decoder on this platform or the file cannot be read.
	bool probe_video(std::span<const std::byte> bytes, VideoInfo& info, std::string& error);
}
