-- bc7enc_rdo (Richard Geldreich, MIT or public domain): the BC7 block encoder only. Used by the editor's texture importer.
project "bc7enc"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"bc7enc.cpp",
		"bc7enc.h",
		"bc7decomp.cpp",
		"bc7decomp.h"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		optimize "Speed" -- third-party: unoptimized, a 2k texture takes minutes
		runtimechecks "Off"

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "Off"

	filter "configurations:Shipping"
		runtime "Release"
		optimize "On"
		symbols "Off"
