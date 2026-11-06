#pragma once

#include <optional>
#include <string>

#include <core/common.h>

namespace wiwa
{
	// The working set of this process, in bytes. 0 when the system does not say.
	u64 process_ram_bytes();

	struct VideoMemory
	{
		u64 UsedBytes = 0;
		u64 BudgetBytes = 0;
	};

	// The device-local memory this process uses and may use on the DXGI adapter named adapterName (the name the backends report). None
	// when no adapter has that name or the query fails; that is logged once per name.
	std::optional<VideoMemory> query_video_memory(const std::string& adapterName);
}
