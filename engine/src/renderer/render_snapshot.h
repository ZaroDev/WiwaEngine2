#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <math/math.h>

namespace wiwa
{
	// What the render thread tells the main thread, for the UI. Plain data, copied on every read.
	struct RenderSnapshot
	{
		double FrameMs = 0.0;
		double Fps = 0.0; // of the render thread, averaged over half a second
		std::string Api{};
		std::string Adapter{};
		std::string ShaderStatus{};
		std::vector<std::pair<std::string, double>> PassMs{};
		bool Vsync = true;
		u32 ErrorCount = 0;
	};

	// The render thread publishes, the main thread reads: whoever reads gets the latest complete snapshot.
	class SnapshotChannel
	{
	public:
		void publish(RenderSnapshot snapshot)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Snapshot = std::move(snapshot);
		}

		RenderSnapshot read() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return m_Snapshot;
		}

	private:
		mutable std::mutex m_Mutex{};
		RenderSnapshot m_Snapshot{};
	};
}
