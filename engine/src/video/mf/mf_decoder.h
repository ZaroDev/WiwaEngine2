#pragma once

#include <core/common.h>

#include "../video_decoder.h"

// The Windows decoder, on Media Foundation's source reader (mp4, m4v, mov: H.264 and AAC come with Windows). No Media Foundation or COM type is
// visible outside this folder, and this folder is compiled only for Windows.
namespace wiwa
{
	// COM (multithreaded) and Media Foundation are started by open() on the calling thread and ended by close() on the same thread: open and
	// close a decoder from one thread (the player's decode worker, or the importer's probe).
	Scope<IVideoDecoder> create_mf_decoder();
}
