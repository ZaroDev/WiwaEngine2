project "wiwa"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	
	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	pchheader "pch.h"
	pchsource "src/pch.cpp"

	ignoredefaultlibraries 
	{
		"libcmtd"
	}
	
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
		"%{IncludeDirs.LZ4}"
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
		"dxguid",
		"vk-bootstrap",
		"ImGui",
		-- "Optick",
		-- "ImGuizmo",
	}

	defines
	{
		"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1"
	}

	filter "files:vendor/imguizmo/**.cpp"
		flags {"NoPCH"}

	filter "system:windows"
		staticruntime "Off"
		systemversion "latest"
		defines
		{
			"WI_PLATFORM_WINDOWS",
			"GLFW_INCLUDE_NONE",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS"
		}


		postbuildcommands
		{
			--"{COPY} \"%{cfg.buildtarget.relpath}\" \"%{wks.location}Editor\"",
			-- "copy \"..\\bin\\%{outputdir}\\Wiwa\\Wiwa.lib\" \"%{wks.location}Editor\\game\\Wiwa.lib\"",
			-- "copy \"..\\bin\\%{outputdir}\\Wiwa\\Wiwa.lib\" \"%{wks.location}Editor\\game_assembly\\Wiwa.lib\""
		}

	debugdir "%{wks.location}/Editor"
	
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
		