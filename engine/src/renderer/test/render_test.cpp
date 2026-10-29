#include <pch.h>
#include "render_test.h"

#include "../render_world.h"
#include "../renderer.h"
#include "../upload_queue.h"

namespace wiwa
{
	namespace
	{
		struct NamedCheck
		{
			std::string Name;
			RenderCheck Run;
		};

		std::vector<NamedCheck>& checks()
		{
			static std::vector<NamedCheck> list;
			return list;
		}

		u8 pattern_byte(u64 index, u32 salt)
		{
			return static_cast<u8>((index * 31 + 7 + salt * 5) & 0xff);
		}

		// The same buffer and mip-mapped texture as copy_queue_roundtrip, written through an UploadQueue.
		bool upload_queue_roundtrip(nvrhi::IDevice* device, UploadQueue& queue, std::string& error)
		{
			constexpr u32 BUFFER_BYTES = 4096;
			constexpr u32 TEXTURE_SIZE = 64;
			constexpr u32 MIP_COUNT = 3;

			nvrhi::BufferDesc bufferDescription;
			bufferDescription.byteSize = BUFFER_BYTES;
			bufferDescription.initialState = UploadQueue::resting_state(device);
			bufferDescription.keepInitialState = true;
			bufferDescription.debugName = "upload queue buffer";
			nvrhi::BufferHandle buffer = device->createBuffer(bufferDescription);

			nvrhi::TextureDesc textureDescription;
			textureDescription.width = TEXTURE_SIZE;
			textureDescription.height = TEXTURE_SIZE;
			textureDescription.mipLevels = MIP_COUNT;
			textureDescription.format = nvrhi::Format::RGBA8_UNORM;
			textureDescription.isShaderResource = true;
			textureDescription.initialState = UploadQueue::resting_state(device);
			textureDescription.keepInitialState = true;
			textureDescription.debugName = "upload queue texture";
			nvrhi::TextureHandle texture = device->createTexture(textureDescription);
			if (!buffer || !texture)
			{
				error = "could not create the resources";
				return false;
			}

			std::vector<u8> bufferData(BUFFER_BYTES);
			for (u32 i = 0; i < BUFFER_BYTES; i++)
			{
				bufferData[i] = pattern_byte(i, 0);
			}
			std::vector<std::vector<u8>> mipData(MIP_COUNT);

			queue.begin_frame();
			queue.write_buffer(buffer, bufferData.data(), bufferData.size());
			for (u32 mip = 0; mip < MIP_COUNT; mip++)
			{
				const u32 size = TEXTURE_SIZE >> mip;
				mipData[mip].resize(static_cast<size_t>(size) * size * 4);
				for (size_t i = 0; i < mipData[mip].size(); i++)
				{
					mipData[mip][i] = pattern_byte(i, mip + 1);
				}
				queue.write_texture(texture, 0, mip, mipData[mip].data(), static_cast<size_t>(size) * 4);
			}
			queue.submit();

			std::vector<u8> readBack;
			if (!read_buffer_bytes(device, buffer, BUFFER_BYTES, readBack) || readBack != bufferData)
			{
				error = "the buffer does not hold what was uploaded";
				return false;
			}
			for (u32 mip = 0; mip < MIP_COUNT; mip++)
			{
				u32 width = 0;
				u32 height = 0;
				if (!read_texture_rgba8(device, texture, readBack, width, height, mip) || readBack != mipData[mip])
				{
					error = "texture mip " + std::to_string(mip) + " does not hold what was uploaded";
					return false;
				}
			}
			return true;
		}

		void register_all_checks()
		{
			register_scene_checks();
			register_cache_checks();
			register_viewport_checks();
			register_camera_checks();
			register_forward_checks();
			register_light_checks();
			register_shadow_checks();
			register_blend_checks();
			register_scale_checks();
			register_ibl_checks();

			register_render_check("copy_queue_roundtrip", [](RenderCheckContext& context)
				{
					WI_LOG(Info, "RenderTest", "copy queue available: {0}", context.HasCopyQueue ? "yes" : "no");
					std::string error;
					const bool graphicsOk = copy_queue_roundtrip(context.Device, false, error);
					context.check(graphicsOk, "graphics queue upload: " + error);
					if (context.HasCopyQueue)
					{
						error.clear();
						const bool copyOk = copy_queue_roundtrip(context.Device, true, error);
						context.check(copyOk, "copy queue upload: " + error);
					}
				});

			register_render_check("upload_buffer_and_texture", [](RenderCheckContext& context)
				{
					UploadQueue queue;
					context.check(queue.initialize(context.Device, context.HasCopyQueue, 1024 * 1024), "initialize");
					for (const bool forceGraphics : { false, true })
					{
						queue.force_graphics_queue(forceGraphics);
						std::string error;
						const bool ok = upload_queue_roundtrip(context.Device, queue, error);
						context.check(ok, std::string(forceGraphics ? "graphics queue: " : "default queue: ") + error);
					}
					queue.shutdown();
				});

			register_render_check("upload_budget", [](RenderCheckContext& context)
				{
					UploadQueue queue;
					context.check(queue.initialize(context.Device, context.HasCopyQueue, 1000), "initialize");

					nvrhi::BufferDesc description;
					description.byteSize = 600;
					description.initialState = UploadQueue::resting_state(context.Device);
					description.keepInitialState = true;
					nvrhi::BufferHandle buffer = context.Device->createBuffer(description);
					const std::vector<u8> data(600, 1);

					queue.begin_frame();
					context.check(queue.has_room(5000), "an upload larger than the budget is allowed when nothing was written");
					context.check(queue.has_room(500), "room before any write");
					queue.write_buffer(buffer, data.data(), data.size());
					context.check(queue.bytes_this_frame() == 600, "bytes counted");
					context.check(!queue.has_room(500), "no room for 500 more after 600 of 1000");
					context.check(queue.has_room(400), "room for exactly the rest");
					queue.submit();
					queue.begin_frame();
					context.check(queue.has_room(500), "room again after begin_frame");
					queue.shutdown();
				});

			register_render_check("upload_reports_queue", [](RenderCheckContext& context)
				{
					UploadQueue queue;
					context.check(queue.initialize(context.Device, context.HasCopyQueue, 1024), "initialize");
					context.check(queue.is_using_copy_queue() == context.HasCopyQueue, "uses the copy queue exactly when the backend has one");
					queue.force_graphics_queue(true);
					context.check(!queue.is_using_copy_queue(), "forced to the graphics queue");
					queue.force_graphics_queue(false);
					context.check(queue.is_using_copy_queue() == context.HasCopyQueue, "back to the default");
					queue.shutdown();
				});

			register_render_check("readback_clear_color", [](RenderCheckContext& context)
				{
					nvrhi::TextureDesc description;
					description.width = 64;
					description.height = 32;
					description.format = nvrhi::Format::RGBA8_UNORM;
					description.isRenderTarget = true;
					description.initialState = nvrhi::ResourceStates::RenderTarget;
					description.keepInitialState = true;
					description.debugName = "readback_clear_color target";
					nvrhi::TextureHandle target = context.Device->createTexture(description);
					nvrhi::CommandListHandle commandList = context.Device->createCommandList();
					context.check(target && commandList, "could not create the target");
					if (!target || !commandList)
					{
						return;
					}

					commandList->open();
					commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.5f, 0.75f, 1.0f));
					commandList->close();
					context.Device->executeCommandList(commandList);

					std::vector<u8> pixels;
					u32 width = 0;
					u32 height = 0;
					context.check(read_texture_rgba8(context.Device, target, pixels, width, height), "readback failed");
					context.check(width == 64 && height == 32 && pixels.size() == 64 * 32 * 4, "wrong readback size");

					const u8 expected[4] = { 64, 128, 191, 255 };
					bool allEqual = pixels.size() == 64 * 32 * 4;
					for (size_t i = 0; allEqual && i < pixels.size(); i++)
					{
						allEqual = std::abs(static_cast<int>(pixels[i]) - static_cast<int>(expected[i % 4])) <= 1;
					}
					context.check(allEqual, "every pixel is (64, 128, 191, 255) within 1");
				});
		}
	}

	void RenderCheckContext::check(bool condition, const std::string& what)
	{
		if (!condition)
		{
			Failures++;
			WI_LOG(Error, "RenderTest", "{0}: check failed: {1}", CurrentCheck, what);
		}
	}

	void register_render_check(std::string name, RenderCheck check)
	{
		checks().push_back({ std::move(name), std::move(check) });
	}

	bool run_render_checks(RenderWorld& world)
	{
		checks().clear();
		register_all_checks();

		Renderer& renderer = world.get_renderer();
		RenderCheckContext context(world);
		context.Device = renderer.get_device();
		context.Api = renderer.get_api();
		context.HasCopyQueue = renderer.has_copy_queue();

		u32 failedChecks = 0;
		for (const NamedCheck& named : checks())
		{
			context.CurrentCheck = named.Name;
			const u32 before = context.Failures;
			named.Run(context);
			const bool passed = context.Failures == before;
			if (passed)
			{
				WI_LOG(Info, "RenderTest", "{0}: ok", named.Name);
			}
			else
			{
				WI_LOG(Error, "RenderTest", "{0}: FAILED", named.Name);
				failedChecks++;
			}
		}

		return failedChecks == 0;
	}

	bool read_buffer_bytes(nvrhi::IDevice* device, nvrhi::IBuffer* buffer, u64 byteCount, std::vector<u8>& bytes)
	{
		nvrhi::BufferDesc stagingDescription;
		stagingDescription.byteSize = byteCount;
		stagingDescription.cpuAccess = nvrhi::CpuAccessMode::Read;
		stagingDescription.debugName = "read_buffer_bytes staging";
		nvrhi::BufferHandle staging = device->createBuffer(stagingDescription);
		nvrhi::CommandListHandle commandList = device->createCommandList();
		if (!staging || !commandList)
		{
			return false;
		}

		commandList->open();
		commandList->copyBuffer(staging, 0, buffer, 0, byteCount);
		commandList->close();
		device->executeCommandList(commandList);
		device->waitForIdle();

		const void* mapped = device->mapBuffer(staging, nvrhi::CpuAccessMode::Read);
		if (mapped == nullptr)
		{
			return false;
		}
		bytes.assign(static_cast<const u8*>(mapped), static_cast<const u8*>(mapped) + byteCount);
		device->unmapBuffer(staging);
		return true;
	}

	bool read_texture_rgba8(nvrhi::IDevice* device, nvrhi::ITexture* texture, std::vector<u8>& pixels, u32& width, u32& height, u32 mip)
	{
		const nvrhi::TextureDesc& description = texture->getDesc();
		width = std::max(description.width >> mip, 1u);
		height = std::max(description.height >> mip, 1u);

		nvrhi::TextureDesc stagingDescription;
		stagingDescription.width = width;
		stagingDescription.height = height;
		stagingDescription.format = description.format;
		stagingDescription.debugName = "read_texture_rgba8 staging";
		nvrhi::StagingTextureHandle staging = device->createStagingTexture(stagingDescription, nvrhi::CpuAccessMode::Read);
		nvrhi::CommandListHandle commandList = device->createCommandList();
		if (!staging || !commandList)
		{
			return false;
		}

		nvrhi::TextureSlice source;
		source.mipLevel = mip;
		source.width = width;
		source.height = height;
		nvrhi::TextureSlice destination;
		destination.width = width;
		destination.height = height;

		commandList->open();
		commandList->copyTexture(staging, destination, texture, source);
		commandList->close();
		device->executeCommandList(commandList);
		device->waitForIdle();

		size_t rowPitch = 0;
		const u8* mapped = static_cast<const u8*>(device->mapStagingTexture(staging, destination, nvrhi::CpuAccessMode::Read, &rowPitch));
		if (mapped == nullptr)
		{
			return false;
		}
		pixels.resize(static_cast<size_t>(width) * height * 4);
		for (u32 y = 0; y < height; y++)
		{
			std::memcpy(pixels.data() + static_cast<size_t>(y) * width * 4, mapped + y * rowPitch, static_cast<size_t>(width) * 4);
		}
		device->unmapStagingTexture(staging);
		return true;
	}

	bool copy_queue_roundtrip(nvrhi::IDevice* device, bool useCopyQueue, std::string& error)
	{
		const nvrhi::ResourceStates restState = UploadQueue::resting_state(device);

		constexpr u32 BUFFER_BYTES = 64 * 1024;
		constexpr u32 TEXTURE_SIZE = 128;
		constexpr u32 MIP_COUNT = 3;

		nvrhi::BufferDesc bufferDescription;
		bufferDescription.byteSize = BUFFER_BYTES;
		bufferDescription.initialState = restState; // a known state between lists: they run on different queues
		bufferDescription.keepInitialState = true;
		bufferDescription.debugName = "roundtrip buffer";
		nvrhi::BufferHandle buffer = device->createBuffer(bufferDescription);

		nvrhi::TextureDesc textureDescription;
		textureDescription.width = TEXTURE_SIZE;
		textureDescription.height = TEXTURE_SIZE;
		textureDescription.mipLevels = MIP_COUNT;
		textureDescription.format = nvrhi::Format::RGBA8_UNORM;
		textureDescription.isShaderResource = true;
		textureDescription.initialState = restState;
		textureDescription.keepInitialState = true;
		textureDescription.debugName = "roundtrip texture";
		nvrhi::TextureHandle texture = device->createTexture(textureDescription);

		nvrhi::CommandListParameters parameters;
		parameters.setQueueType(useCopyQueue ? nvrhi::CommandQueue::Copy : nvrhi::CommandQueue::Graphics);
		nvrhi::CommandListHandle upload = device->createCommandList(parameters);
		if (!buffer || !texture || !upload)
		{
			error = "could not create the resources or the upload list";
			return false;
		}

		std::vector<u8> bufferData(BUFFER_BYTES);
		for (u32 i = 0; i < BUFFER_BYTES; i++)
		{
			bufferData[i] = pattern_byte(i, 0);
		}

		std::vector<std::vector<u8>> mipData(MIP_COUNT);
		upload->open();
		upload->writeBuffer(buffer, bufferData.data(), bufferData.size());
		for (u32 mip = 0; mip < MIP_COUNT; mip++)
		{
			const u32 size = TEXTURE_SIZE >> mip;
			mipData[mip].resize(static_cast<size_t>(size) * size * 4);
			for (size_t i = 0; i < mipData[mip].size(); i++)
			{
				mipData[mip][i] = pattern_byte(i, mip + 1);
			}
			upload->writeTexture(texture, 0, mip, mipData[mip].data(), static_cast<size_t>(size) * 4);
		}
		upload->close();

		if (useCopyQueue)
		{
			const u64 instance = device->executeCommandList(upload, nvrhi::CommandQueue::Copy);
			device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Copy, instance);
		}
		else
		{
			device->executeCommandList(upload);
		}

		std::vector<u8> readBack;
		if (!read_buffer_bytes(device, buffer, BUFFER_BYTES, readBack) || readBack != bufferData)
		{
			error = "the buffer does not hold what was uploaded";
			return false;
		}

		for (u32 mip = 0; mip < MIP_COUNT; mip++)
		{
			u32 width = 0;
			u32 height = 0;
			if (!read_texture_rgba8(device, texture, readBack, width, height, mip) || readBack != mipData[mip])
			{
				error = "texture mip " + std::to_string(mip) + " does not hold what was uploaded";
				return false;
			}
		}
		return true;
	}
}
