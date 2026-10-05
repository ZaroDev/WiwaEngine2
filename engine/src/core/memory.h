#pragma once
#include <memory>

namespace wiwa
{
	static uint64_t s_AllocationCount = 0;
	static uint64_t s_AllocatedBytesSize = 0;
}

template<typename T>
using Scope = std::unique_ptr<T>;
template<typename T, typename ... Args>
constexpr Scope<T> CreateScope(Args&& ... args)
{
	return std::make_unique<T>(std::forward<Args>(args)...);
}
template<typename T>
using Ref = std::shared_ptr<T>;
template<typename T, typename ... Args>
constexpr Ref<T> CreateRef(Args&& ... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename C, typename P>
constexpr Ref<C> DynCast(Ref<P> ref)
{
	return std::dynamic_pointer_cast<C>(ref);
}

template<typename C, typename P>
constexpr Ref<C> Cast(Ref<P> ref)
{
	return std::static_pointer_cast<C>(ref);
}

void* operator new(std::size_t count)
{
	if (count == 0)
	{
		return nullptr;
	}
	wiwa::s_AllocationCount++;
	wiwa::s_AllocatedBytesSize += count;

	return std::malloc(count);
}
void* operator new[](std::size_t count)
{
	if (count == 0)
	{
		return nullptr;
	}
	wiwa::s_AllocationCount++;
	wiwa::s_AllocatedBytesSize += count;

	return std::malloc(count);
}


void operator delete(void* pointer) noexcept
{
	if (pointer)
	{
		wiwa::s_AllocationCount--;
		wiwa::s_AllocatedBytesSize -= sizeof(pointer);
		std::free(pointer);
	}
}
void operator delete[](void* pointer) noexcept
{
	if (pointer)
	{
		wiwa::s_AllocationCount--;
		wiwa::s_AllocatedBytesSize -= sizeof(pointer);
		std::free(pointer);
	}
}