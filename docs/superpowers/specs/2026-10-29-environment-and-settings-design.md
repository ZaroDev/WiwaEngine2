# Environment and Settings (sub-project 2c-i) Design

Part of the renderer integration (`PROMPT.md`). Done so far: asset system, 2a Foundation, 2b-i Scene foundation, 2b-ii Render path (Forward+, shadows, copy-queue uploads, `--render-test`). 2c (post-processing and settings panels) is split in three; this spec is **2c-i: scene settings, panels, and the sky with image-based lighting**. 2c-ii (exposure adaptation, bloom, tonemap options, AO, FXAA) and 2c-iii (TAA, motion blur, fog, SSR, color LUT, user effects) follow, each with their own spec.

## Goal and success criteria

A scene has a real sky and lighting from it: an `.hdr` Environment asset or the procedural atmosphere, drawn behind the geometry and lighting it (diffuse SH, prefiltered specular, BRDF table). The render settings are split between the scene (the look) and the project (quality) and edited in separate panels.

- `--render-test` and `--render-smoke` exit 0 on Vulkan, D3D11 and D3D12 with GPU validation; each new check family is shown to fail under a negative control.
- Debug, Release and Shipping build; the game project links no editor code.
- A scene with each sky source shows correctly in the editor Viewport and in the game; old `.wscene` files load with default settings.

## Decisions carried in

- **Slicing:** three sub-projects (this is the first).
- **Where settings live:** per scene (the look: environment, ambient, background, exposure) in a dedicated `SceneSettings` block inside `SceneData`; engine/project-wide (quality: shadows, vsync, uploads) in a project file. Chosen over storing settings as reflected components.
- **Sky:** `.hdr` Environment asset plus procedural atmosphere. Clouds, time of day and the moon are out.
- **IBL build:** cooked at import (the `.hdr` importer runs the IBL math). The atmosphere has no file, so it runs the same math on a job thread at run time.
- Conventions carried over: `wiwa` namespace, `WI_LOG`, no `std::cout`, tabs, premake only, shaders in `editor/engine/shaders` cooked by the editor to DXBC and SPIR-V, components and settings store `AssetRef`, the render thread never calls `Engine::get_module`, no build-config macro decides editor vs game, `docs/superpowers/` is not committed.

## Scene settings

`struct SceneSettings` (in `scene/`, main-thread owned by `Scene`, serialized in `SceneData`):

- **Environment:** `Source` (None, File, Atmosphere), `EnvironmentAsset` (`AssetRef`, for File), `RotationDegrees`, `Brightness`, `AmbientIntensity`.
- **Atmosphere:** the sky parameters of the standalone's `SkyParameters` that the user may change (turbidity and the like; the list is fixed in the plan from `world/AtmosphereSettings.h`). The sun direction is the scene's first directional light; there is no separate sun.
- **Background:** `Sky` or a solid color.
- **Exposure:** manual, in EV.
- Today's `render::SceneSettings` (hemisphere colors, background, exposure, shadow values) is split: look values move here, shadow values move to quality.

**Formats:** appended to the binary payload and to the `.wscene` JSON as a `settings` object; a file without it gets defaults; the scene format version is bumped. Unknown values are kept as in 2b-i.

**To the render thread:** the main thread owns the active scene's settings, editor panels edit them, `SceneExtractor` compares them with what it last sent and puts changes in the render batch with the other deltas (replacing `RenderModule::set_scene_settings`). `ResetScene` sends them too.

## Quality settings

`RenderQuality`: shadow distance, cascade count, split lambda, depth and normal bias, spot shadow bias, vsync, copy queue on/off. A JSON file next to the program (`render_quality.json`) is read when the renderer starts and written when the Quality panel changes a value. A missing or unreadable file gives defaults (logged once). The render thread receives it as a command and applies it at the start of the next frame.

## Environment asset and IBL

- **`EnvironmentData` (cooked):** the cube (face size, RGBA16F, mip chain; mip 0 is the sky, the other mips are the roughness-prefiltered reflections), the 9 SH coefficients, the source size. The raw equirectangular pixels are not kept.
- **Importer:** `.hdr` import runs `IblMath` (ported from the standalone's `lighting/IblMath.*`, in the engine library so the importer and the runtime share it). The importer version goes up; existing `.hdr` files re-import.
- **BRDF table:** scene-independent; built once at renderer start on a job thread (a small RG16F texture).
- **`EnvironmentCache` (render thread, beside `GpuResourceCache`):** keyed by the Environment guid. Loads through the `AssetManager`, uploads the cube, SH and BRDF table through the `UploadQueue` (copy queue) within the frame budget, drops the CPU copy, and follows the 2a reload listener for hot reload (old environment used until the new one is ready). A failed or missing asset logs once and the lighting falls back to the hemisphere ambient.
- **Atmosphere:** a job thread builds cube + SH from `AtmosphereEnvironmentDescription` (from the standalone's `AtmosphereEnvironment.h` and `AtmosphereMath.h`) when the sun direction or parameters differ from the built one by more than half a degree; a request replaces one that has not started, the running one finishes first, the old environment stays in use until the new one is uploaded. A failed build keeps the previous environment.

## Frame changes (render thread)

- **Sky pass:** after the opaque forward pass, before the blended stage. File: draws the cube (mip 0) rotated and scaled by the settings where depth is still 1. Atmosphere: analytic sky shader (from `sky.hlsl` / `Atmosphere.hlsli`) drawn every frame, so the sun moves smoothly. Solid-color background: the HDR clear color, no sky pass. Depth test equal to the far plane, no depth write.
- **Forward shader:** the ambient term is the diffuse SH plus the prefiltered specular (mip by roughness) times the BRDF table, times `AmbientIntensity`, times occlusion. With Source = None it keeps today's hemisphere ambient. The environment bindings join the frame set (cube, BRDF table, sampler; SH in the frame constants).
- **Background clear** now comes from the settings' background mode.
- No reflection probes: reflections are the sky only. Spot and point lights are unchanged.

## Panels

Each its own class in `editor/src/render_panels/`, shown as separate windows; the main-thread panels edit the active scene's `SceneSettings` and mark the scene dirty (saved with Save, like Hierarchy and Inspector edits).

- **Renderer:** stays as the stats and status window.
- **Environment:** source selector, Environment asset field (a path text box until an asset browser exists), rotation, brightness, atmosphere parameters, build status (building, ready, failed with the reason).
- **Lighting:** ambient intensity, background mode and color, exposure (EV).
- **Quality:** the quality settings above; edits save to the project file.

## Errors and degradation

- Unreadable or missing `.hdr`, or a wrong asset type: logged once, status text in the panel, hemisphere ambient.
- Atmosphere build failure: previous environment kept, status says why.
- Bad or missing quality file: defaults, logged once.
- Old `.wscene`: default settings.
- Device refuses a texture or the sampler: clear log, the scene renders with the hemisphere ambient; the engine keeps running.

## Testing

`--render-test` (extended) and `--render-smoke`, three APIs, GPU validation on, plus deliberate-break negative controls.

- **IBL math:** a constant environment gives SH that reproduce the color; the prefiltered mips conserve energy within a tolerance; BRDF table values against references.
- **Asset:** a cooked Environment round-trips through the asset system; a file cooked with the old format version re-imports.
- **Settings:** `SceneSettings` round-trips through binary and JSON; an old file loads as defaults; the extractor sends only changes.
- **Render checks:** a uniform white environment lights a diffuse sphere evenly; a rotated environment moves a highlight; the sky pass shows the environment behind geometry and the solid background does not; the atmosphere sun changes the sky and the ambient; an environment replaced while the old one is uploading does not crash; a failed asset falls back; the same images on all three APIs.
- **Quality:** the file round-trips; a bad file gives defaults.
- **Acceptance:** screenshots of the panels, the sample scene with an `.hdr` and with the atmosphere (editor and game), Debug/Release/Shipping builds.

## Out of scope

Clouds, time of day and moon, reflection probes, auto-exposure, bloom, tonemap options, AO, FXAA, TAA, motion blur, fog, SSR, color LUT, user effects (2c-ii and 2c-iii); an asset browser; point-light shadows.

## Task order (for the plan)

1. `IblMath` port with unit checks.
2. `SceneSettings` model, binary and JSON formats, extractor deltas, replacing the old settings command.
3. `RenderQuality` file and the Quality plumbing to the render thread.
4. Environment asset: cooked format, importer, version bump.
5. BRDF table and `EnvironmentCache` (uploads, reload, fallback).
6. Forward shader IBL and the background mode.
7. Sky pass for files.
8. Atmosphere: description, job-thread build, analytic sky shader, rebuild policy.
9. Panels (Environment, Lighting, Quality) and Renderer panel status.
10. Scale, smoke, cross-backend, hot reload and acceptance.
