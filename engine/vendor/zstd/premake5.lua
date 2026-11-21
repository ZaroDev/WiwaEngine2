-- zstd 1.5.6 (BSD): the library without the legacy decoders, the dictionary builder and the assembly decoder. The game needs the
-- decoder (assets.wpak); the editor's packager the encoder.
project "zstd"
	kind "StaticLib"
	language "C"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/zstd.h",
		"src/zstd_errors.h",
		"src/common/*.c",
		"src/common/*.h",
		"src/compress/*.c",
		"src/compress/*.h",
		"src/decompress/*.c",
		"src/decompress/*.h"
	}

	defines { "ZSTD_DISABLE_ASM", "ZSTD_MULTITHREAD=0" }

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		optimize "Speed" -- third-party: level 19 is far too slow unoptimized
		runtimechecks "Off"

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "Off"

	filter "configurations:Shipping"
		runtime "Release"
		optimize "On"
		symbols "Off"
