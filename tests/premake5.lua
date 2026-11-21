-- Every check of the engine and the editor, in one executable: the headless smokes, the panel, play, demo and VFX smokes, and the GPU render checks.
-- Like the editor it hosts the editor module, so it compiles the editor's sources (editor/src, minus the editor's own entry point) next to its own
-- and links the engine only. The editor and the game contain no test code. Run it from editor/ (see CLAUDE.md).
project "wiwa_tests"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
		"%{wks.location}/editor/src/**.h",
		"%{wks.location}/editor/src/**.cpp",
		"%{wks.location}/editor/vendor/ImGuizmo/ImGuizmo.h",
		"%{wks.location}/editor/vendor/ImGuizmo/ImGuizmo.cpp",
		"%{wks.location}/editor/vendor/ImGuizmo/ImCurveEdit.h",
		"%{wks.location}/editor/vendor/ImGuizmo/ImCurveEdit.cpp",
		"%{wks.location}/editor/vendor/ImGuizmo/GraphEditor.h",
		"%{wks.location}/editor/vendor/ImGuizmo/GraphEditor.cpp"
	}

	-- the editor's entry point: the tests have their own
	removefiles
	{
		"%{wks.location}/editor/src/editor.cpp"
	}

	includedirs
	{
		"src",
		"%{wks.location}/editor/src",
		"%{wks.location}/editor/vendor/ImGuizmo",
		"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.nvrhi}",
		"%{IncludeDirs.NJSON}",
		"%{IncludeDirs.STB}",
		"%{IncludeDirs.CGLTF}",
		"%{IncludeDirs.UFBX}",
		"%{IncludeDirs.LZ4}",
		"%{IncludeDirs.zstd}",
		"%{IncludeDirs.bc7enc}",
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.filewatch}",
		"%{IncludeDirs.SPDLOG}",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.Wiwa}",
		"%{IncludeDirs.sol2}",
		"%{IncludeDirs.lua}",
		"%{IncludeDirs.Recast}",
		"%{IncludeDirs.Detour}",
		-- the render checks reach into the renderer's own headers
		"%{IncludeDirs.DirectXHeaders}",
		"%{IncludeDirs.VulkanSDK}",
		"%{IncludeDirs.vkbootstrap}",
		"%{IncludeDirs.GLFW}",
		"%{IncludeDirs.Jolt}",
		"%{IncludeDirs.miniaudio}",
		"%{IncludeDirs.DetourCrowd}"
	}

	links
	{
		"wiwa",
		"ImGui",
		"LZ4",
		"zstd",
		"bc7enc",
		"ufbx",
		"Recast",
		"d3dcompiler"
	}

	-- the render checks include engine internals: the engine's own definitions
	defines
	{
		"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1"
	}

	JoltDefines()
	SolDefines()
	WwiseDefines()
	WwiseLinks()

	-- the editor's sources are compiled here without the editor's flags: the same sources as in wiwa_editor
	filter "files:**/vendor/ImGuizmo/**.cpp"
		warnings "Off"
	filter {}

	-- the code the engine marks as large objects
	filter "files:**/scene_components.cpp"
		buildoptions { "/bigobj" }
	filter {}

	debugdir "%{wks.location}/editor"

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
			("copy /Y \"$(TargetPath)\" \"%{wks.location}editor\\wiwa_tests.exe\"")
		}

	filter "configurations:Debug"
		defines { "WI_DEBUG", "WI_ENABLE_ASSERTS" }
		kind "ConsoleApp"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "WI_RELEASE"
		kind "ConsoleApp"
		runtime "Release"
		symbols "On"
		optimize "On"

	-- a test run is never windowed
	filter "configurations:Shipping"
		defines "WI_SHIPPING"
		kind "ConsoleApp"
		runtime "Release"
		symbols "Off"
		optimize "On"
