workspace "Wiwa2"
	architecture "x86_64"
	startproject "wiwa_editor"
	configurations
	{
		"Debug",
		"Release",
		"Shipping"
	}

	multiprocessorcompile "On"

	defines
	{
		"_CRT_SECURE_NO_WARNINGS"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

VULKAN_SDK = os.getenv("VULKAN_SDK")
-- The Wwise SDK is optional: with WWISESDK set the Wwise audio backend is built (WI_AUDIO_WWISE); without it only the Null backend exists.
-- This is a feature toggle, not an editor-vs-game decision.
WWISESDK = os.getenv("WWISESDK")

IncludeDirs = {}
IncludeDirs["Wiwa"] = "%{wks.location}/engine/src"
IncludeDirs["VulkanSDK"] = "%{VULKAN_SDK}/Include"
IncludeDirs["GLFW"] = "%{wks.location}/engine/vendor/GLFW/include"
IncludeDirs["nvrhi"] = "%{wks.location}/engine/vendor/nvrhi/include"
IncludeDirs["DirectXHeaders"] = "%{wks.location}/engine/vendor/nvrhi/thirdparty/DirectX-Headers/include"
IncludeDirs["vkbootstrap"] = "%{wks.location}/engine/vendor/vk-bootstrap/src"
IncludeDirs["ImGui"] = "%{wks.location}/engine/vendor/imgui"
IncludeDirs["GLM"] = "%{wks.location}/engine/vendor/glm"
IncludeDirs["ENTT"] = "%{wks.location}/engine/vendor/entt"
IncludeDirs["NJSON"] = "%{wks.location}/engine/vendor/nlohmann/include"
IncludeDirs["SPDLOG"] = "%{wks.location}/engine/vendor/spdlog/include"
IncludeDirs["STB"] = "%{wks.location}/engine/vendor/stb"
IncludeDirs["CGLTF"] = "%{wks.location}/engine/vendor/cgltf"
IncludeDirs["UFBX"] = "%{wks.location}/engine/vendor/ufbx"
IncludeDirs["bc7enc"] = "%{wks.location}/engine/vendor/bc7enc"
IncludeDirs["zstd"] = "%{wks.location}/engine/vendor/zstd/src"
IncludeDirs["LZ4"] = "%{wks.location}/engine/vendor/lz4/src"
IncludeDirs["filewatch"] = "%{wks.location}/engine/vendor/filewatch"
IncludeDirs["lua"] = "%{wks.location}/engine/vendor/lua"
IncludeDirs["sol2"] = "%{wks.location}/engine/vendor/sol2/include"
IncludeDirs["Jolt"] = "%{wks.location}/engine/vendor/JoltPhysics"
IncludeDirs["miniaudio"] = "%{wks.location}/engine/vendor/miniaudio"
IncludeDirs["Recast"] = "%{wks.location}/engine/vendor/recastnavigation/Recast/Include"
IncludeDirs["Detour"] = "%{wks.location}/engine/vendor/recastnavigation/Detour/Include"
IncludeDirs["DetourCrowd"] = "%{wks.location}/engine/vendor/recastnavigation/DetourCrowd/Include"

LibraryDirs = {}
LibraryDirs["VulkanSDK"] = "%{VULKAN_SDK}/Lib"

-- Wwise: the compile definition and include path for every project that sees the Wwise backend (the engine, and the editor and game for the
-- checks and startup that name it).
function WwiseDefines()
	if WWISESDK then
		defines { "WI_AUDIO_WWISE" }
		includedirs { "%{WWISESDK}/include" }
	end
end

-- Wwise static libraries for the executables (the engine is a static library). The dynamic-CRT SDK folders match staticruntime "Off".
function WwiseLinks()
	if WWISESDK then
		filter "configurations:Debug"
			libdirs { "%{WWISESDK}/x64_vc170/Debug/lib" }
		filter "configurations:not Debug"
			libdirs { "%{WWISESDK}/x64_vc170/Release/lib" }
		filter {}
		links
		{
			-- the core of the sound engine and the Vorbis codec banks use by default; a project that uses effect or source plug-ins adds their
			-- libraries here and their factory headers in engine/src/audio/wwise/wwise_backend.cpp
			"AkSoundEngine", "AkMemoryMgr", "AkStreamMgr", "AkSpatialAudio", "AkVorbisDecoder",
			"ws2_32", "dsound", "dxguid", "xinput", "winmm"
		}
	end
end

-- sol2 checks the types of the arguments a script passes (a string where a number or an entity goes is a Lua error) only in debug builds unless told
-- to: without this a Release build coerces them or casts unchecked. Every project that includes sol needs the same define.
function SolDefines()
	defines { "SOL_ALL_SAFETIES_ON=1" }
end

-- Jolt compile definitions: identical for the Jolt lib and for every project that includes Jolt headers (a mismatch is an ODR bug).
-- JPH_OBJECT_STREAM is deliberately not defined and cross-platform determinism is off.
function JoltDefines()
	filter "configurations:Debug"
		defines { "JPH_DEBUG_RENDERER", "JPH_PROFILE_ENABLED", "JPH_ENABLE_ASSERTS" }
	filter "configurations:Release"
		defines { "JPH_DEBUG_RENDERER", "JPH_PROFILE_ENABLED" }
	filter {}
end

group "Dependencies"
	include "engine/vendor/GLFW"
	include "engine/vendor/nvrhi"
	include "engine/vendor/vk-bootstrap"
	include "engine/vendor/imgui"
	include "engine/vendor/lz4"
	include "engine/vendor/zstd"
	include "engine/vendor/bc7enc"
	include "engine/vendor/ufbx"
	include "engine/vendor/lua"
	include "engine/vendor/JoltPhysics"
	include "engine/vendor/recastnavigation"
group ""

group "Core"
	include "engine/premake5.lua"
group ""

include "editor/premake5.lua"
include "game/premake5.lua"
include "tests/premake5.lua"
