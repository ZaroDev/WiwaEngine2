project "wiwa_editor"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
		"vendor/ImGuizmo/ImGuizmo.h",
		"vendor/ImGuizmo/ImGuizmo.cpp",
		"vendor/ImGuizmo/ImCurveEdit.h",
		"vendor/ImGuizmo/ImCurveEdit.cpp",
		"vendor/ImGuizmo/GraphEditor.h",
		"vendor/ImGuizmo/GraphEditor.cpp"
	}

	includedirs
	{
		"src",
		"vendor/ImGuizmo",
		"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.nvrhi}", -- render_module.h exposes NVRHI types
		"%{IncludeDirs.NJSON}",
		"%{IncludeDirs.STB}",
		"%{IncludeDirs.CGLTF}",
		"%{IncludeDirs.UFBX}",
		"%{IncludeDirs.LZ4}",
		"%{IncludeDirs.zstd}",
		"%{IncludeDirs.bc7enc}",
		"%{IncludeDirs.meshoptimizer}",
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.filewatch}",
		"%{IncludeDirs.SPDLOG}",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.Wiwa}",
		"%{IncludeDirs.sol2}",
		"%{IncludeDirs.lua}",
		"%{IncludeDirs.Recast}",
		"%{IncludeDirs.Detour}"
	}

	links
	{
		"wiwa",
		"ImGui",
		"LZ4",
		"zstd",
		"bc7enc",
		"meshoptimizer",
		"ufbx",
		"Recast",
		"d3dcompiler"
	}

	WwiseDefines()
	WwiseLinks()

	debugdir "%{wks.location}/editor"

	SolDefines()

	filter "system:windows"
		staticruntime "Off"
		systemversion "latest"

		defines
		{
			"WI_PLATFORM_WINDOWS"
		}
		postbuildcommands
		{
			("copy /Y \"$(TargetPath)\" \"%{wks.location}editor\\wiwa_editor.exe\"")
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
