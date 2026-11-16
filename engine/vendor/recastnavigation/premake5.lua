-- recastnavigation v1.6.0 (VERSION.txt). Recast bakes navmeshes (the editor only); Detour and DetourCrowd query and steer (the engine).
local function navlib(name, extraIncludes)
	project(name)
		kind "StaticLib"
		language "C++"
		cppdialect "C++20"
		staticruntime "Off"

		targetdir ("bin/" .. outputdir .. "/%{prj.name}")
		objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

		files
		{
			name .. "/Include/**.h",
			name .. "/Source/**.cpp"
		}

		includedirs(extraIncludes)

		filter "system:windows"
			systemversion "latest"

		filter "configurations:Debug"
			runtime "Debug"
			symbols "On"

		filter "configurations:Release"
			runtime "Release"
			optimize "On"
			symbols "Off"

		filter "configurations:Shipping"
			runtime "Release"
			optimize "On"
			symbols "Off"
		filter {}
end

navlib("Recast", { "Recast/Include" })
navlib("Detour", { "Detour/Include" })
navlib("DetourCrowd", { "DetourCrowd/Include", "Detour/Include" })
