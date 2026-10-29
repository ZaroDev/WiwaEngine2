#pragma once

#include <map>
#include <optional>

#include <math/math.h>

namespace wiwa
{
	// First-fit allocator of ranges in a linear space (vertex or index buffer elements). Freed neighbours are merged.
	class RangeAllocator
	{
	public:
		RangeAllocator() = default;
		explicit RangeAllocator(u64 capacity) : m_Capacity(capacity) { m_Free[0] = capacity; }

		std::optional<u64> allocate(u64 count)
		{
			if (count == 0)
			{
				return std::nullopt;
			}
			for (auto it = m_Free.begin(); it != m_Free.end(); ++it)
			{
				if (it->second >= count)
				{
					const u64 offset = it->first;
					const u64 remaining = it->second - count;
					m_Free.erase(it);
					if (remaining > 0)
					{
						m_Free[offset + count] = remaining;
					}
					m_Used += count;
					return offset;
				}
			}
			return std::nullopt;
		}

		void free(u64 offset, u64 count)
		{
			if (count == 0)
			{
				return;
			}
			m_Used -= count;

			auto next = m_Free.lower_bound(offset);
			if (next != m_Free.begin())
			{
				auto previous = std::prev(next);
				if (previous->first + previous->second == offset)
				{
					offset = previous->first;
					count += previous->second;
					m_Free.erase(previous);
				}
			}
			if (next != m_Free.end() && offset + count == next->first)
			{
				count += next->second;
				m_Free.erase(next);
			}
			m_Free[offset] = count;
		}

		u64 capacity() const { return m_Capacity; }
		u64 used() const { return m_Used; }

	private:
		std::map<u64, u64> m_Free{}; // offset -> count
		u64 m_Capacity = 0;
		u64 m_Used = 0;
	};
}
