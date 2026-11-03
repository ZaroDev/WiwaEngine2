#include <pch.h>
#include "jolt_runtime.h"
#include "jolt_config.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>

namespace wiwa
{
	namespace
	{
		std::mutex s_RuntimeMutex;
		u32 s_RuntimeUsers = 0;

		void jolt_trace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			WI_PHYSICS_LOG(Info, "Jolt: {0}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool jolt_assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			WI_PHYSICS_LOG(Error, "Jolt assert {0}:{1}: ({2}) {3}", file, line, expression, message != nullptr ? message : "");
			return true; // break, as Jolt does by default
		}
#endif
	}

	void acquire_jolt_runtime()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeMutex);
		if (s_RuntimeUsers++ == 0)
		{
			JPH::RegisterDefaultAllocator();
			JPH::Trace = jolt_trace;
			JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = jolt_assert_failed;)
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}
	}

	void release_jolt_runtime()
	{
		std::lock_guard<std::mutex> lock(s_RuntimeMutex);
		if (--s_RuntimeUsers == 0)
		{
			JPH::UnregisterTypes();
			delete JPH::Factory::sInstance;
			JPH::Factory::sInstance = nullptr;
		}
	}
}
