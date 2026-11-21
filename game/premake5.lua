-- A game built on the engine: it links the engine only, nothing of the editor. The editor and the game are two projects.
project "wiwa_game"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{wks.location}/bin/".. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/".. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
		"src",
		"%{IncludeDirs.Wiwa}",
		"%{IncludeDirs.GLM}",
		"%{IncludeDirs.SPDLOG}",
		"%{IncludeDirs.ENTT}",
		"%{IncludeDirs.nvrhi}", -- render_module.h exposes NVRHI types
		"%{IncludeDirs.ImGui}",
		"%{IncludeDirs.LZ4}",
		"%{IncludeDirs.zstd}"
	}

	links
	{
		"wiwa",
		"ImGui",
		"LZ4",
		"zstd"
	}

	WwiseDefines()
	WwiseLinks()

	debugdir "%{wks.location}/game"

	filter "system:windows"
		staticruntime "Off"
		systemversion "latest"

		defines
		{
			"WI_PLATFORM_WINDOWS"
		}
		postbuildcommands
		{
			("copy /Y \"$(TargetPath)\" \"%{wks.location}game\\wiwa_game.exe\"")
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
