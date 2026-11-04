#pragma once

namespace wiwa
{
	// Jolt's process-wide state (allocator hooks, factory, registered types): set up by the first user, torn down by the last. A world and
	// the shape cooker each hold it while they use Jolt. Thread safe.
	void acquire_jolt_runtime();
	void release_jolt_runtime();

	struct JoltRuntimeScope
	{
		JoltRuntimeScope() { acquire_jolt_runtime(); }
		~JoltRuntimeScope() { release_jolt_runtime(); }
		JoltRuntimeScope(const JoltRuntimeScope&) = delete;
		JoltRuntimeScope& operator=(const JoltRuntimeScope&) = delete;
	};
}
