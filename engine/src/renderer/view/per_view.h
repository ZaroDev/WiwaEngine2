#pragma once

#include <vector>

#include "../viewport_target.h"
#include "view_types.h"

namespace wiwa
{
	// What a pass caches for one view (framebuffers, binding sets, history indices). A pass keeps one PerView<Cache> instead of a single
	// "built for this generation" marker, so that alternating between views does not rebuild everything every time. The slot of a view is
	// reset to a fresh Cache whenever its target was created again. Render thread only.
	template<class T>
	class PerView
	{
	public:
		// The slot of the view this target belongs to. Reset when the target's generation changed since the slot was last handed out.
		T& get(const ViewportTarget& target)
		{
			m_Clock++;
			for (Slot& slot : m_Slots)
			{
				if (slot.Uid == target.uid())
				{
					slot.LastUsed = m_Clock;
					if (slot.Generation != target.generation())
					{
						slot.Value = T{};
						slot.Generation = target.generation();
					}
					return slot.Value;
				}
			}

			if (m_Slots.size() >= MAX_VIEWS)
			{
				size_t oldest = 0;
				for (size_t i = 1; i < m_Slots.size(); i++)
				{
					if (m_Slots[i].LastUsed < m_Slots[oldest].LastUsed)
					{
						oldest = i;
					}
				}
				m_Slots.erase(m_Slots.begin() + static_cast<std::ptrdiff_t>(oldest));
			}
			Slot slot;
			slot.Uid = target.uid();
			slot.Generation = target.generation();
			slot.LastUsed = m_Clock;
			m_Slots.push_back(std::move(slot));
			return m_Slots.back().Value;
		}

		// Drops every slot (the device is going away, or the pass is rebuilt).
		void clear() { m_Slots.clear(); }

		// Drops the slot of a target that is gone.
		void forget(u32 uid)
		{
			for (size_t i = 0; i < m_Slots.size(); i++)
			{
				if (m_Slots[i].Uid == uid)
				{
					m_Slots.erase(m_Slots.begin() + static_cast<std::ptrdiff_t>(i));
					return;
				}
			}
		}

	private:
		struct Slot
		{
			u32 Uid = 0;
			u32 Generation = ~0u;
			u64 LastUsed = 0;
			T Value{};
		};
		std::vector<Slot> m_Slots{};
		u64 m_Clock = 0;
	};
}
