#pragma once

#include <video/video_types.h>

namespace wiwa
{
	class RenderWorld;

	// The pictures of a playing video on the GPU. Render thread only. One BGRA8 texture per stream, registered in the TextureRegistry under
	// view_video_texture_id(stream) (renderer/view/view_types.h), which is what a UiBatch of a Video widget names.

	// Writes the picture into the stream's texture, making the texture (or making it again when the size changed). A picture without a size or
	// without all its pixels is ignored.
	void upload_video_picture(RenderWorld& world, u32 stream, const VideoFrame& frame);

	// Lets go of the stream's texture. Nothing happens for a stream that has none.
	void release_video_picture(RenderWorld& world, u32 stream);
}
