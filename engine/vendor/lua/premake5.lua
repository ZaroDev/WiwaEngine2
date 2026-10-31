project "lua"
    kind "StaticLib"
    language "C"
    cdialect "C99"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    -- Core, auxiliary, and standard-library sources.
    -- lua.c is intentionally excluded: it builds the standalone interpreter.
    files {
        "lapi.c",
        "lcode.c",
        "lctype.c",
        "ldebug.c",
        "ldo.c",
        "ldump.c",
        "lfunc.c",
        "lgc.c",
        "llex.c",
        "lmem.c",
        "lobject.c",
        "lopcodes.c",
        "lparser.c",
        "lstate.c",
        "lstring.c",
        "ltable.c",
        "ltm.c",
        "lundump.c",
        "lvm.c",
        "lzio.c",
        "ltests.c",

        "lauxlib.c",

        "lbaselib.c",
        "ldblib.c",
        "liolib.c",
        "lmathlib.c",
        "loslib.c",
        "ltablib.c",
        "lstrlib.c",
        "lutf8lib.c",
        "loadlib.c",
        "lcorolib.c",
        "linit.c",

        "*.h"
    }

   
    filter "system:linux"
        links { "dl", "m" }
         defines {
        "LUA_USE_LINUX"
        }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "Off"

    filter "platforms:x86"
        architecture "x86"

    filter "platforms:x86_64"
        architecture "x86_64"

    filter {}
