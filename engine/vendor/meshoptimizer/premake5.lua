-- meshoptimizer 1.3 (MIT, Arseny Kapoulkine): the mesh simplifier behind the editor's LOD baking. Editor only; the runtime reads the baked indices.
project "meshoptimizer"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/*.cpp",
		"src/meshoptimizer.h"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		optimize "Speed" -- third-party: simplifying a big mesh unoptimized takes minutes
		runtimechecks "Off"

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "Off"

	filter "configurations:Shipping"
		runtime "Release"
		optimize "On"
		symbols "Off"
