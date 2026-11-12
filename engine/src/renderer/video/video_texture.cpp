#include <pch.h>
#include "video_texture.h"

#include "../render_world.h"
#include "../view/view_types.h"

namespace wiwa
{
	void upload_video_picture(RenderWorld& world, u32 stream, const VideoFrame& frame)
	{
		if (frame.Width == 0 || frame.Height == 0 || frame.Width > VIDEO_MAX_DIMENSION || frame.Height > VIDEO_MAX_DIMENSION ||
			frame.Bgra.size() < static_cast<size_t>(frame.Width) * frame.Height * 4)
		{
			return;
		}
		nvrhi::IDevice* device = world.get_renderer().get_device();
		if (device == nullptr)
		{
			return;
		}

		const u32 id = view_video_texture_id(stream);
		TextureRegistry& textures = world.get_renderer().get_textures();

		nvrhi::TextureHandle texture;
		if (nvrhi::ITexture* existing = textures.get(id))
		{
			const nvrhi::TextureDesc& description = existing->getDesc();
			if (description.width == frame.Width && description.height == frame.Height)
			{
				texture = existing;
			}
		}
		if (!texture)
		{
			nvrhi::TextureDesc description;
			description.width = frame.Width;
			description.height = frame.Height;
			description.format = nvrhi::Format::BGRA8_UNORM; // what a decoder gives; not sRGB: the UI blends display values
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = "Video picture";
			texture = device->createTexture(description);
			if (!texture)
			{
				return;
			}
			textures.set(id, texture); // the registry holds it (and drops the one of another size)
		}

		nvrhi::CommandListHandle upload = device->createCommandList();
		upload->open();
		upload->writeTexture(texture, 0, 0, frame.Bgra.data(), static_cast<size_t>(frame.Width) * 4);
		upload->close();
		device->executeCommandList(upload);
	}

	void release_video_picture(RenderWorld& world, u32 stream)
	{
		world.get_renderer().get_textures().remove(view_video_texture_id(stream));
	}
}
