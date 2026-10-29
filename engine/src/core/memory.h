#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace wiwa
{
	// Signed so a free of a block we never counted shows up as a negative value instead of wrapping to the max
	inline std::atomic<int64_t> s_AllocationCount{ 0 };
	inline std::atomic<int64_t> s_AllocatedBytesSize{ 0 };
}

inline void* wiwa_alloc(std::size_t count)
{
	void* pointer = std::malloc(count == 0 ? 1 : count);
	if (!pointer)
	{
		throw std::bad_alloc();
	}
	wiwa::s_AllocationCount.fetch_add(1, std::memory_order_relaxed);
	wiwa::s_AllocatedBytesSize.fetch_add(static_cast<int64_t>(_msize(pointer)), std::memory_order_relaxed);
	return pointer;
}

inline void wiwa_free(void* pointer) noexcept
{
	if (pointer)
	{
		wiwa::s_AllocationCount.fetch_sub(1, std::memory_order_relaxed);
		wiwa::s_AllocatedBytesSize.fetch_sub(static_cast<int64_t>(_msize(pointer)), std::memory_order_relaxed);
		std::free(pointer);
	}
}

void* operator new(std::size_t count) { return wiwa_alloc(count); }
void* operator new[](std::size_t count) { return wiwa_alloc(count); }
void operator delete(void* pointer) noexcept { wiwa_free(pointer); }
void operator delete[](void* pointer) noexcept { wiwa_free(pointer); }
