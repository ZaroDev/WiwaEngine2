#include <pch.h>
#include "system_memory.h"

#include <set>

#include <Windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace wiwa
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		std::string to_utf8(const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
			{
				return {};
			}
			std::string result(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		std::optional<VideoMemory> fail(const std::string& adapterName, const char* why)
		{
			static std::set<std::string> reported;
			if (reported.insert(adapterName).second)
			{
				WI_LOG(Warning, "Perf", "video memory of '{0}' is unavailable: {1}", adapterName, why);
			}
			return std::nullopt;
		}
	}

	u64 process_ram_bytes()
	{
		PROCESS_MEMORY_COUNTERS counters{};
		counters.cb = sizeof(counters);
		if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		{
			return 0;
		}
		return static_cast<u64>(counters.WorkingSetSize);
	}

	std::optional<VideoMemory> query_video_memory(const std::string& adapterName)
	{
		ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			return fail(adapterName, "no DXGI factory");
		}

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
		{
			DXGI_ADAPTER_DESC1 description{};
			if (SUCCEEDED(adapter->GetDesc1(&description)) && to_utf8(description.Description) == adapterName)
			{
				ComPtr<IDXGIAdapter3> adapter3;
				DXGI_QUERY_VIDEO_MEMORY_INFO info{};
				if (FAILED(adapter.As(&adapter3)) || FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
				{
					return fail(adapterName, "QueryVideoMemoryInfo failed");
				}
				return VideoMemory{ info.CurrentUsage, info.Budget };
			}
			adapter.Reset();
		}
		return fail(adapterName, "no DXGI adapter has that name");
	}
}
