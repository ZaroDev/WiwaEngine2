project "wiwa"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	pchheader "pch.h"
	pchsource "src/pch.cpp"

	ignoredefaultlibraries { "libcmtd" }

	files
	{
		"src/**.hpp",
		"src/**.h",
		"src/**.cpp",
	}

	includedirs
	{
		"src",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.SPDLOG}",
		"%{IncludeDirs.GLFW}",
		"%{IncludeDirs.nvrhi}",
		"%{IncludeDirs.DirectXHeaders}",
		"%{IncludeDirs.VulkanSDK}",
		"%{IncludeDirs.vkbootstrap}",
		"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.LZ4}",
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.NJSON}",
		"%{IncludeDirs.lua}",
		"%{IncludeDirs.sol2}",
		"%{IncludeDirs.Jolt}",
		"%{IncludeDirs.miniaudio}",
		"%{IncludeDirs.Detour}",
		"%{IncludeDirs.DetourCrowd}",
	}

	links
	{
		"LZ4",
		"GLFW",
		"nvrhi",
		"%{LibraryDirs.VulkanSDK}/vulkan-1.lib",
		"d3d11",
		"d3d12",
		"dxgi",
		"psapi",
		"dxguid",
		"vk-bootstrap",
		"ImGui",
		"lua",
		"Jolt",
		"Detour",
		"DetourCrowd"
	}

	defines
	{
		"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1"
	}

	-- must match the Jolt library defines
	JoltDefines()
	SolDefines()
	WwiseDefines()

	-- the miniaudio implementation is one big vendor file: no precompiled header, and not our warnings
	filter "files:src/audio/mini/miniaudio_impl.cpp"
		enablepch "Off"
		warnings "Off"

	-- every component description of the reflection registry is one big function
	filter "files:src/scene/scene_components.cpp"
		buildoptions { "/bigobj" }

	-- the sol2 usertypes of the Lua bindings make big object files
	filter "files:src/scripting/bindings/**.cpp"
		buildoptions { "/bigobj" }

	-- the video decoder of Windows (Media Foundation) lives in src/video/mf and is built only there; another platform adds its own folder and its case in
	-- src/video/video_factory.cpp
	filter "system:not windows"
		removefiles { "src/video/mf/**" }

	filter "system:windows"
		staticruntime "Off"
		systemversion "latest"
		links
		{
			"mfplat",
			"mfreadwrite",
			"mfuuid",
			"ole32",
			"shlwapi"
		}
		defines
		{
			"WI_PLATFORM_WINDOWS",
			"GLFW_INCLUDE_NONE",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS"
		}

	filter "configurations:Debug"
		defines
		{
			"WI_DEBUG",
			"WI_ENABLE_ASSERTS"
		}
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "WI_RELEASE"
		runtime "Release"
		optimize "On"
		symbols "On"

	filter "configurations:Shipping"
		defines "WI_SHIPPING"
		runtime "Release"
		optimize "On"
		symbols "Off"
