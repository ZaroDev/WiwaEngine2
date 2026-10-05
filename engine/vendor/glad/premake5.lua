project "glad"
kind "StaticLib"
language "C"

targetdir("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
objdir("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

includedirs {"include"}
files {"include/glad/glad.h", "include/KHR/khrplatform.h", "src/glad.c"}
