project "LZ4"
	kind "StaticLib"
	language "C"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/lz4.c",
		"src/lz4.h",
		"src/lz4hc.c",
		"src/lz4hc.h"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		optimize "Speed" -- third-party: the high-compression mode is far too slow unoptimized
		flags { "NoRuntimeChecks" }

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "Off"

	filter "configurations:Shipping"
		runtime "Release"
		optimize "On"
		symbols "Off"
