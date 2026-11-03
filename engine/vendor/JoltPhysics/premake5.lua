project "Jolt"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "Off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"Jolt/**.h",
		"Jolt/**.inl",
		"Jolt/**.cpp"
	}

	includedirs
	{
		"."
	}

	-- the defines must match the ones of every project that includes Jolt headers (see JoltDefines in the root premake5.lua)
	JoltDefines()

	filter "system:windows"
		systemversion "latest"
		buildoptions { "/Zc:__cplusplus" }

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
