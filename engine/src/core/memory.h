#pragma once


namespace wiwa
{
	static uint64_t s_AllocationCount = 0;
	static uint64_t s_AllocatedBytesSize = 0;
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