#include <pch.h>
#include "bindings.h"

#include <core/engine.h>

namespace wiwa
{
	void bind_engine(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::table time = runtime.state().create_table();
		// seconds: of the update that is running, and the sum of all of them since the scripts started
		time.set_function("delta", [&context]() { return context.DeltaTime; });
		time.set_function("total", [&context]() { return static_cast<float>(context.TotalTime); });
		runtime.sandbox()["time"] = time;

		// the project's identity, as it was when the scripts started (read-only data, not a handle)
		const ProjectSettings& settings = Engine::get().project();
		sol::table project = runtime.state().create_table();
		project["name"] = settings.Name;
		project["company"] = settings.Company;
		project["description"] = settings.Description;
		project["version"] = version_string(settings);
		project["version_major"] = settings.VersionMajor;
		project["version_minor"] = settings.VersionMinor;
		project["version_patch"] = settings.VersionPatch;
		runtime.sandbox()["project"] = project;

		// asks the host to end the program: the game closes, the editor stops play mode
		runtime.sandbox().set_function("quit", [&context]()
			{
				if (!context.RequestQuit)
				{
					throw sol::error("quit is not available here");
				}
				context.RequestQuit();
			});
	}
}
