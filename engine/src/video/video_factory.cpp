#include <pch.h>
#include "video_factory.h"

#ifdef WI_PLATFORM_WINDOWS
#include "mf/mf_decoder.h"
#endif

namespace wiwa
{
	Scope<IVideoDecoder> create_platform_decoder()
	{
#ifdef WI_PLATFORM_WINDOWS
		return create_mf_decoder();
#else
		return nullptr;
#endif
	}

	bool probe_video(std::span<const std::byte> bytes, VideoInfo& info, std::string& error)
	{
		Scope<IVideoDecoder> decoder = create_platform_decoder();
		if (!decoder)
		{
			error = "this platform has no video decoder";
			return false;
		}
		const bool ok = decoder->open(bytes, info, error);
		decoder->close();
		return ok;
	}
}
