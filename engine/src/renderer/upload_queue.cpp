#include <pch.h>
#include "upload_queue.h"

namespace wiwa
{
	bool UploadQueue::initialize(nvrhi::IDevice* device, bool copyQueueAvailable, size_t bytesPerFrameBudget)
	{
		m_Device = device;
		m_Budget = bytesPerFrameBudget;

		nvrhi::CommandListParameters graphicsParameters;
		graphicsParameters.setQueueType(nvrhi::CommandQueue::Graphics);
		m_GraphicsList = device->createCommandList(graphicsParameters);
		if (!m_GraphicsList)
		{
			WI_LOG(Error, "Renderer", "could not create the upload command list");
			return false;
		}

		if (copyQueueAvailable)
		{
			nvrhi::CommandListParameters copyParameters;
			copyParameters.setQueueType(nvrhi::CommandQueue::Copy);
			m_CopyList = device->createCommandList(copyParameters);
			if (!m_CopyList)
			{
				WI_LOG(Warning, "Renderer", "could not create a copy queue command list, uploads use the graphics queue");
			}
		}

		force_graphics_queue(false);
		return true;
	}

	void UploadQueue::shutdown()
	{
		m_List = nullptr;
		m_CopyList = nullptr;
		m_GraphicsList = nullptr;
		m_Device = nullptr;
		m_Open = false;
	}

	nvrhi::ResourceStates UploadQueue::resting_state(nvrhi::IDevice* device)
	{
		return device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 ? nvrhi::ResourceStates::Common : nvrhi::ResourceStates::CopyDest;
	}

	void UploadQueue::force_graphics_queue(bool forced)
	{
		if (m_Open)
		{
			submit(); // what was written so far goes out on the queue it was recorded for
		}

		m_UsingCopyQueue = !forced && m_CopyList != nullptr;
		m_List = m_UsingCopyQueue ? m_CopyList.Get() : m_GraphicsList.Get();
	}

	void UploadQueue::begin_frame()
	{
		m_BytesThisFrame = 0;
		m_LastSubmitMs = 0.0;
	}

	bool UploadQueue::has_room(size_t bytes) const
	{
		return m_BytesThisFrame == 0 || m_BytesThisFrame + bytes <= m_Budget;
	}

	bool UploadQueue::ensure_open()
	{
		if (m_List == nullptr)
		{
			return false;
		}
		if (!m_Open)
		{
			m_OpenedAt = std::chrono::steady_clock::now();
			m_List->open();
			m_Open = true;
		}
		return true;
	}

	void UploadQueue::write_buffer(nvrhi::IBuffer* buffer, const void* data, size_t bytes, u64 offset)
	{
		if (!ensure_open())
		{
			return;
		}
		m_List->writeBuffer(buffer, data, bytes, offset);
		m_BytesThisFrame += bytes;
		m_BytesTotal += bytes;
	}

	void UploadQueue::write_texture(nvrhi::ITexture* texture, u32 arraySlice, u32 mip, const void* data, size_t rowPitch)
	{
		if (!ensure_open())
		{
			return;
		}
		m_List->writeTexture(texture, arraySlice, mip, data, rowPitch);

		const nvrhi::TextureDesc& description = texture->getDesc();
		const u32 blockSize = nvrhi::getFormatInfo(description.format).blockSize;
		const u32 rows = (std::max(description.height >> mip, 1u) + blockSize - 1) / blockSize;
		m_BytesThisFrame += rowPitch * rows;
		m_BytesTotal += rowPitch * rows;
	}

	void UploadQueue::submit(u64 graphicsInstance)
	{
		if (!m_Open)
		{
			return;
		}

		m_List->close();
		m_Open = false;

		if (m_UsingCopyQueue)
		{
			if (graphicsInstance != 0)
			{
				m_Device->queueWaitForCommandList(nvrhi::CommandQueue::Copy, nvrhi::CommandQueue::Graphics, graphicsInstance);
			}
			const u64 instance = m_Device->executeCommandList(m_List, nvrhi::CommandQueue::Copy);
			m_Device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Copy, instance);
		}
		else
		{
			m_Device->executeCommandList(m_List); // the graphics queue runs it before anything submitted after it
		}

		m_LastSubmitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_OpenedAt).count();
	}
}
