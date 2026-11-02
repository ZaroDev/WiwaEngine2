# Building without msbuild: Premake gmake2 + LLVM-mingw

Date: 2026-11-02. Status: design, awaiting review.

## Goal

`wiwa`, `wiwa_editor` and `wiwa_game` build on Windows with no Visual Studio and no msbuild: Premake generates Makefiles (`gmake2`), compiled by clang from an LLVM-mingw toolchain and driven by `make`. Premake stays the single source of truth; the `vs2022` path keeps working unchanged.

## Decisions (from the brainstorm)

- Toolchain: LLVM-mingw (clang, lld, mingw-w64 headers/import libs, libc++ or libstdc++ as shipped), x86_64 / UCRT.
- Approach: a toolchain-aware gmake2 path inside the existing premake scripts, not a hand-written Makefile and not CMake.
- **Vendor code is not touched**: nothing under `engine/vendor/` (sources or its `premake5.lua` files) is edited. Survey found no MSVC-only flags in those premake scripts (only `runtime`, `staticruntime`, `systemversion`, which a clang toolset ignores), so none are expected to need overriding. If one turns out to, it is overridden from the root `premake5.lua` (reopen the project after `include`), never edited in place.
- Out of scope: shader cooking (the editor's DXC step is unchanged), committing toolchain binaries, CI.

## Design

### 1. Premake changes (root, `engine/`, `editor/`, `game/` scripts only)

Add filters on `toolset "clang"` / `system:windows` for the MSVC-only bits, leaving the existing `vs2022` behaviour intact:

- `/bigobj` in `engine/premake5.lua` (sol2 bindings): add the clang equivalent `-Wa,-mbig-obj` under the clang filter.
- Link libs: `vulkan-1.lib` via `%{LibraryDirs.VulkanSDK}`: under clang link `vulkan-1` (see §3); `d3d11`, `d3d12`, `dxgi`, `dxguid`, `d3dcompiler` resolve through mingw-w64 import libs unchanged.
- `ignoredefaultlibraries "libcmtd"`: guarded to MSVC only.
- PCH (`pch.h`): premake's gmake2 clang PCH works with `pchheader`; keep it, verify `/Yu`-free path.
- Config defines (`WI_DEBUG`, `WI_RELEASE`, `WI_SHIPPING`, `WI_PLATFORM_WINDOWS`) are toolset-independent and stay.
- `{COPY}` postbuild steps and `debugdir` are already portable.

A `--cc=clang` premake option selects the toolset; default remains MSVC for `vs2022`.

### 2. Source fixes (engine/editor/game only)

Compile errors that appear under clang-mingw are fixed in first-party code only. The survey found MSVC-specific usage confined to `engine/src/renderer/backend/d3d11_backend.*`, `d3d12_backend.*` and `editor/src/asset_pipeline/shader_compiler.cpp` (COM/ComPtr/DXC), which mingw-w64 supports (`wrl/client.h`, `dxcapi.h` from the Windows SDK or Vulkan SDK). A vendor incompatibility (NVRHI, sol2, ImGui, etc.) is not patched: it is reported, and the fix is expressed as a first-party flag/define/include-order change or the item is escalated to the user.

### 3. Vulkan import library

`vulkan-1.lib` from the Vulkan SDK is an MSVC import library. The script resolves this by generating a mingw import lib from `vulkan-1.dll` (gendef + llvm-dlltool, both in llvm-mingw) into `bin-int/` on first build, instead of relying on the `.lib`.

### 4. Driver script

`scripts/build_mingw.bat` (and a `.sh` is not needed):

1. Locates llvm-mingw via `LLVM_MINGW` env var or `vendor/llvm-mingw` (git-ignored, documented download URL; script can fetch/unzip it on request).
2. Locates `make` (from the same ecosystem, or MSYS2/`mingw32-make`) and fails with a clear message if absent.
3. Runs `vendor\premake\premake5.exe gmake2 --cc=clang`.
4. Runs `make config=<cfg>_x86_64 -j%NUMBER_OF_PROCESSORS%` for the requested config (`Debug` default; `Release`, `Shipping` accepted).

The generated Makefiles are git-ignored like `*.sln`. `CLAUDE.md` gets a short "Building without msbuild" section.

### 5. Verification

Built executables are checked with the existing smoke commands: `wiwa_editor --asset-smoke`, `--scene-smoke`, `--script-smoke`, `--render-test` (exit code 0), plus `wiwa_game --scene-smoke-load`. Per project convention a result is only trusted if a negative control fails (e.g. a deliberately broken asset makes `--asset-smoke` exit non-zero). Also check that the msbuild path still builds (no regression to the `vs2022` output).

## Risks

- Vendor incompatibility with clang-mingw (NVRHI D3D12 / DirectX-Headers, sol2): cannot be patched; mitigation is first-party flags or escalation.
- Mixed C++ runtime or ABI between prebuilt MSVC libs and mingw objects: none of the linked libs are prebuilt MSVC apart from the Vulkan import lib (handled in §3).
- Nothing in this environment currently has clang, make or ninja, so the first task is acquiring llvm-mingw and make.

## Done when

`scripts\build_mingw.bat Debug` and `Release` produce the three binaries from a clean checkout with no VS installed, the smoke commands above pass, and no file under `engine/vendor/` shows in `git status`.
