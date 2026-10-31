#include <pch.h>
#include "bindings.h"

namespace wiwa
{
	void bind_engine(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::table time = runtime.state().create_table();
		// seconds: of the update that is running, and the sum of all of them since the scripts started
		time.set_function("delta", [&context]() { return context.DeltaTime; });
		time.set_function("total", [&context]() { return static_cast<float>(context.TotalTime); });
		runtime.sandbox()["time"] = time;
	}
}
