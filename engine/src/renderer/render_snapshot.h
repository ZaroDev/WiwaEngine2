#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <math/math.h>

#include "view/view_types.h"

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
		bool HasCamera = false; // the scene has a camera to draw with (the editor's camera counts)
		std::string EnvironmentStatus{}; // of the scene's environment: none, loading, ready, failed: <why>
		u32 SceneInstances = 0;     // in the render-side scene
		u32 SceneLights = 0;
		u32 VisibleInstances = 0;   // inside the camera's view last frame
		u32 DrawCalls = 0;          // opaque + masked + blended primitives drawn by the forward pass
		u32 LodPrimitives[4]{};     // of the main view's draw lists: primitives drawn at each level of detail (0 is the full mesh)
		u32 ShadowViews = 0;
		u32 CacheMeshes = 0;
		u32 CacheMaterials = 0;
		u32 CacheTextures = 0;
		u64 CacheBytes = 0;
		u32 CachePending = 0;
		u32 CacheFailed = 0;
		std::string UploadQueueName{}; // "copy queue" or "graphics queue"
		u64 UploadBytesTotal = 0;
		double UploadMs = 0.0; // the CPU time of the latest frame that uploaded something
		std::vector<ViewId> FailedViews{}; // the offscreen views that could not make their target
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
