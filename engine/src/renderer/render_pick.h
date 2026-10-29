#pragma once

#include <mutex>
#include <optional>

#include <math/math.h>
#include <scene/entity_guid.h>

namespace wiwa
{
	// A click the editor wants answered: which entity is drawn at this pixel of the viewport image ((0, 0) is the top left).
	struct PickRequest
	{
		u64 Id = 0; // echoed in the result
		u32 X = 0;
		u32 Y = 0;
	};

	// The answer: the entity under the pixel, or none (background, outside the viewport, a scene that changed).
	struct PickResult
	{
		u64 RequestId = 0;
		std::optional<EntityGuid> Guid{};
	};

	// The render thread publishes, the main thread takes. Only the latest result is kept.
	class PickChannel
	{
	public:
		void publish(PickResult result)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Result = std::move(result);
		}

		// The latest result, once. A result answering a request older than minimumRequestId is dropped: the caller has asked again since.
		std::optional<PickResult> take(u64 minimumRequestId = 0)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			std::optional<PickResult> result = std::move(m_Result);
			m_Result.reset();
			if (result && result->RequestId < minimumRequestId)
			{
				return std::nullopt;
			}
			return result;
		}

	private:
		std::mutex m_Mutex{};
		std::optional<PickResult> m_Result{};
	};

	// The render thread's picking state: the request waiting for a frame, where the answer goes, and whether the ID target exists (it only
	// does while a request is being answered). Render thread only.
	struct PickState
	{
		std::optional<PickRequest> Pending{};
		PickChannel* Channel = nullptr;
		bool TargetAlive = false;
	};
}
