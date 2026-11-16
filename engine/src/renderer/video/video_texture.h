#pragma once

#include <video/video_types.h>

namespace wiwa
{
	class RenderWorld;
	class UiRenderPass;

	// The pictures of a playing video on the GPU. Render thread only. One BGRA8 texture per stream, registered in the TextureRegistry under
	// view_video_texture_id(stream) (renderer/view/view_types.h), which is what a UiBatch of a Video widget names.

	// Writes the picture into the stream's texture, making the texture (or making it again when the size changed). A picture without a size or
	// without all its pixels is ignored.
	// `ui` (when there is one) forgets the texture that is replaced.
	void upload_video_picture(RenderWorld& world, u32 stream, const VideoFrame& frame, UiRenderPass* ui = nullptr);

	// Lets go of the stream's texture. Nothing happens for a stream that has none.
	// The UI pass caches a binding set per texture it drew, and the set holds the texture: `ui` lets go of it, or the texture would stay on the GPU.
	void release_video_picture(RenderWorld& world, u32 stream, UiRenderPass* ui = nullptr);
}
