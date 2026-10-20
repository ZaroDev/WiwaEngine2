project "vk-bootstrap"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	includedirs
	{
		"%{IncludeDirs.VulkanSDK}",
		"src/**.h"
	}

	files
	{
		"src/**.cpp",
		"src/**.h",
		"src/**.inl"
	}

	filter "system:linux"
		pic "On"
		systemversion "latest"
		

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		optimize "Off"

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "Off"

	filter "configurations:Dist"
		runtime "Release"
		optimize "On"
        symbols "Off"