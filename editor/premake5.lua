project "wiwa_editor"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	files
	{
		-- "resource.h",
		-- "WiwaEditor.rc",
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
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.filewatch}",
		"%{IncludeDirs.SPDLOG}",
		--"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.Wiwa}",
		"%{IncludeDirs.sol2}",
		"%{IncludeDirs.lua}",
		-- "%{IncludeDirs.Optick}",
		-- "%{IncludeDirs.ImGuizmo}",
		-- "%{IncludeDirs.filewatch}",
		-- "%{IncludeDirs.IconFontCppHeaders}",
		-- "%{IncludeDirs.bullet}",
		-- "%{IncludeDirs.ozz}",
		-- "%{IncludeDirs.recastnavigation}",
		-- "%{IncludeDirs.OpenCV}"
	}

	links
	{
		"wiwa",
		"ImGui",
		"LZ4",
		"ufbx",
		"d3dcompiler",
		--"ImGui",
		-- "Optick",
		-- "ImGuizmo"
	}

	WwiseDefines()
	WwiseLinks()

	debugdir "%{wks.location}/editor"

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
