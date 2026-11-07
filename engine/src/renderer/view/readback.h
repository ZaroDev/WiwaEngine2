#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "view_types.h"

namespace wiwa
{
	// Where the render thread puts the pixels of a finished readback, and where the main thread takes them from (RenderModule runs the callbacks
	// of the requests). A request whose view was destroyed has its answer dropped on the main thread: nobody is waiting for it.
	class ReadbackChannel
	{
	public:
		void publish(u64 requestId, ImageData image)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.emplace_back(requestId, std::move(image));
		}

		std::vector<std::pair<u64, ImageData>> take()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			std::vector<std::pair<u64, ImageData>> taken;
			taken.swap(m_Results);
			return taken;
		}

	private:
		std::mutex m_Mutex{};
		std::vector<std::pair<u64, ImageData>> m_Results{};
	};
}
