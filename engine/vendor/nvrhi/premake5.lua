project "nvrhi"
kind "StaticLib"
language "C++"
cppdialect "C++20"

targetdir("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
objdir("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

files {"include/**.h", "src/**.cpp", "src/**.h", "thirdparty/**.h", "thirdparty/**.hpp"}
includedirs {"include", "src", "%{IncludeDirs.VulkanSDK}", "thirdparty/DirectX-Headers/include"}

filter "system:windows"
systemversion "latest"
defines {"VK_USE_PLATFORM_WIN32_KHR", "NOMINMAX"}

filter "configurations:Debug"
runtime "Debug"
symbols "on"

filter "configurations:Release"
runtime "Release"
optimize "on"

