# 2c-ii: Post-processing chain (exposure, bloom, tonemap, FXAA, AO)

Part of the renderer integration (`PROMPT.md`). Done so far: asset system, 2a Foundation, 2b-i Scene foundation, 2b-ii Render path, 2c-i Environment and settings. This spec is **2c-ii**. 2c-iii (TAA, motion blur, fog, SSR, color LUT, user effects) follows with its own spec and builds on the normal target added here.

## Goal

Replace the single ACES `ResolvePass` with an HDR post chain ported from the standalone renderer (`nvrhi-renderer/shaders`: `exposure_*`, `bloom_*`, `tonemap`, `fxaa`, `gtao*`), adapted to this engine's conventions. Success: every effect works on Vulkan, D3D11 and D3D12, images match across the three, and each effect has a `--render-test` check with a negative control.

## Out of scope

Color LUT (2c-iii), TAA, motion blur, fog, SSR, user effects, depth of field, debug views.

## Pass order and targets

```
Shadows -> Depth+Normal prepass -> Light culling -> AO (GTAO depth, GTAO, blur)
  -> Forward opaque -> Sky -> Forward blended
  -> Exposure (histogram, resolve) -> Bloom (down/up chain)
  -> Tonemap+grading (HDR -> LDR) -> FXAA (LDR -> LDR) -> Blit -> ImGui
```

- **Prepass:** the depth prepass gets a second render target: an octahedral-encoded normal in RG16F. Masked materials write it too. This is also the normal source 2c-iii's SSR needs.
- **AO:** GTAO at full resolution on depth + normals, then a blur, into one R8 texture. The forward pass samples it for ambient/indirect terms only, never direct light. With AO off, a 1x1 white texture is bound so the shader has no branch. The AO SRV is added to the material set at t14 (outside the existing t1..t9, t10..t13, s10/s11, s14 ranges).
- **Exposure:** a luminance histogram compute pass feeds a resolve pass that keeps a one-float exposure buffer on the GPU; tonemap reads it. Adaptation uses the render thread's frame delta. With auto off, the manual EV from 2c-i applies. UAVs avoid u6/u7.
- **Bloom:** down/up mip chain with a soft-knee threshold, added to the HDR image inside tonemap.
- **Tonemap:** replaces `ResolvePass`. Per-scene operator (ACES, Reinhard, Neutral, None), then grading (contrast, saturation, temperature, tint, vignette), then sRGB encode.
- **FXAA:** LDR to the second LDR target. When off, Blit reads the tonemap output.
- **Resize:** new targets live in `ViewportTarget` and follow its generation counter; the existing wait-for-idle before target recreation covers them.
- **Bindings:** new passes use their own binding layouts and do not touch the frame set. Any pass using the frame layout must call `setPushConstants`.

## Settings

`SceneSettings` (the look; .wscene `settings` block and binary payload, `sanitized()`, `settings_revision`):

- Exposure: `autoExposure`, `compensationEv`, `key`, `minEv`, `maxEv`, `adaptSpeed`, `lowPercent`, `highPercent`.
- Bloom: `enabled`, `threshold`, `knee`, `intensity`.
- Tonemap: `operator`, `contrast`, `saturation`, `temperature`, `tint`, `vignette`.
- AO: `enabled`, `radius`, `power`.

`RenderQuality` (`render_quality.json`): `bloomMips` (2..6), `aoSlices` (1..4), `aoSteps` (4..16), `fxaa` (bool). Missing keys load defaults; old scene files load default post settings. Values are clamped as in the standalone's `clamp()`.

Settings reach the render thread through the existing scene-settings snapshot; the render thread never touches the ECS or `Engine::get_module`.

## Panels

New panels in `editor/src/panels/`: Exposure, Bloom, Tonemap & Grading, AO. FXAA and the quality values go in the existing Quality panel. The manual exposure slider moves from Lighting to Exposure, which also shows the resolved auto-exposure value read-only.

## Testing

Checks under `engine/src/renderer/test/` via `register_render_check`, hooked through `render_test.h/.cpp`, run with `--render-test --api=<vulkan|d3d11|d3d12> --gpu-validation`, using `SceneTester`:

- Exposure: a bright and a dark scene converge to the same average output luminance.
- Bloom: a bright pixel glows into its neighbors; no change when off.
- Tonemap: each operator maps a gray ramp to known values; neutral grading changes nothing.
- FXAA: a hard edge gains intermediate values.
- AO: a corner is darker than open floor; AO-off equals the baseline.
- Settings round trips: scene save/load and `render_quality.json`, including legacy files without the new keys.

Every task has a negative control (break a line, confirm failure, restore). Cross-backend images must match (mean < 3, max <= 40). Existing prepass checks are re-baselined for the normal target. After editing a shader, run `wiwa_editor.exe --render-smoke 100 --api=d3d11` before `--render-test`.

## Conventions

No `std::cout` (use `WI_LOG`), premake only, no `WI_SHIPPING` for editor/game decisions, `near`/`far` are Windows macros, settings store `AssetRef` not `Handle<T>`. Never commit `PROMPT.md` or `docs/superpowers/`.
