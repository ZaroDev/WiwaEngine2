-- The project launcher: lists, adds, creates and opens projects, then starts the editor on one (launcher.md). Its own small window (GLFW and ImGui's
-- stock D3D11 backend), not the engine's renderer: it links the engine only for the project files (core/project_dir, core/project_settings).
project "wiwa_launcher"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
		"%{wks.location}/engine/vendor/imgui/backends/imgui_impl_glfw.h",
		"%{wks.location}/engine/vendor/imgui/backends/imgui_impl_glfw.cpp",
		"%{wks.location}/engine/vendor/imgui/backends/imgui_impl_dx11.h",
		"%{wks.location}/engine/vendor/imgui/backends/imgui_impl_dx11.cpp"
	}

	includedirs
	{
		"src",
		"%{IncludeDirs.Wiwa}",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.SPDLOG}",
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.NJSON}",
		"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.ImGui}/backends",
		"%{IncludeDirs.GLFW}",
		"%{IncludeDirs.LZ4}",
		"%{IncludeDirs.zstd}"
	}

	links
	{
		"wiwa",
		"ImGui",
		"GLFW",
		"LZ4",
		"zstd",
		"d3d11",
		"dxgi",
		"d3dcompiler",
		"ole32",
		"shell32"
	}

	WwiseDefines()
	WwiseLinks()

	debugdir "%{wks.location}/launcher"

	filter "system:windows"
		staticruntime "Off"
		systemversion "latest"

		defines
		{
			"WI_PLATFORM_WINDOWS",
			"GLFW_INCLUDE_NONE",
			"NOMINMAX"
		}
		postbuildcommands
		{
			("copy /Y \"$(TargetPath)\" \"%{wks.location}launcher\\wiwa_launcher.exe\"")
		}

	filter "configurations:Debug"
		defines "WI_DEBUG"
		kind "ConsoleApp"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "WI_RELEASE"
		runtime "Release"
		kind "ConsoleApp"
		symbols "On"
		optimize "On"

	filter "configurations:Shipping"
		defines "WI_SHIPPING"
		kind "WindowedApp"
		runtime "Release"
		symbols "Off"
		optimize "On"
