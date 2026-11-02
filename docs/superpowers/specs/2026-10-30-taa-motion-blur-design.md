# 2c-iii-A: Velocity, TAA and motion blur

Part of the renderer integration (`PROMPT.md`). Done so far: asset system, 2a, 2b-i, 2b-ii, 2c-i, 2c-ii. 2c-iii is split in three slices, each with its own spec, plan and review: **A (this spec)** velocity prepass + TAA + motion blur; **B** fog + SSR + color LUT; **C** user effects.

## Goal

Add per-object velocity to the Forward+ prepass and build TAA and motion blur on it, ported from the standalone renderer (`shaders/taa.hlsl`, `post_motion_blur.hlsl`, `src/post/TaaMath.h`) but with object motion and strong ghosting rejection. Success: both effects work on Vulkan, D3D11 and D3D12, images match across the three, moving objects leave no visible trail, and every piece has a `--render-test` check with a negative control. With TAA and motion blur off the output is bit-identical to today.

## Out of scope

Fog, SSR, color LUT, user effects, depth of field. Implementing skeletal animation or skinning: it does not exist yet. The velocity path is built so that skinning plugs in (see Skeletal meshes).

## Velocity

- The depth+normal prepass gets a third color target, `ViewportTarget::velocity()` (RG16F): the screen-space UV delta between the object's unjittered current and previous position. Masked materials write it too.
- `RenderInstance` keeps `PrevWorld`. `RenderScene::end_frame()` (called once per rendered frame, after the passes are recorded) sets `PrevWorld = World` for every live instance and remembers whether anything differed, so the instance buffer is rewritten the next frame and an object that stopped gets zero velocity. A new instance starts with `PrevWorld = World`.
- `GpuInstance` becomes `World, Normal, PrevWorld` (192 bytes; mirrored in `SceneInstance` in `scene_common.hlsli`). `FrameConstants` gains `UnjitteredViewProjection`, `PreviousViewProjection` (unjittered) and the jitter of the frame.
- The previous camera resets (velocity from the camera is zero for one frame, history is dropped) on viewport resize, scene reset, a camera cut (a position jump larger than a threshold or a new camera entity) and toggling the editor camera override.
- **Sky and empty pixels** have no geometry; a rotating camera must still move them. The TAA and motion blur shaders reproject depth==far pixels from the camera matrices alone and ignore the velocity target there.
- **Transparent velocity:** after Forward blended a velocity-only pass draws the blended items with an alpha >= 0.5 cutoff, so moving glass and particles reproject with their own motion instead of the opaque surface behind them.

### Skeletal meshes

The velocity vertex shader obtains the previous clip position from one function, `PreviousClipPosition(vertex, instance)` in `depth.hlsli`. Today it applies `PrevWorld`. The skinning slice must keep the previous frame's bone palette per instance and apply it there; this is a stated requirement of that slice. A check calls the hook with a changed world matrix so that a future skinned path cannot silently fall back to world-matrix motion.

## Jitter

While TAA is on, the projection used for rasterising gets a Halton(2,3) sub-pixel offset (8 samples, `apply_jitter` from `TaaMath.h`).

- Unjittered matrices: frustum culling, shadow cascades, picking, the velocity maths, the TAA history lookup.
- Jittered matrices: everything rasterised, and depth reconstruction that must match the depth buffer (AO, sky).
- Shaders reading a projection matrix that the plan must audit one by one: `gtao`, `gtao_depth`, `gtao_blur`, `sky`, `sky_atmosphere`, `pick`, `pick_icon`, `light_cull`, `forward`, `scene_common`.
- With TAA off the jitter is zero and `Projection == UnjitteredProjection`.

## Passes and targets

```
Shadows -> Depth+Normal+Velocity prepass -> Light culling -> AO
  -> Forward opaque -> Sky -> Forward blended -> Transparent velocity
  -> TAA -> Motion blur
  -> Exposure -> Bloom -> Tonemap -> FXAA (only when TAA is off) -> Blit -> ImGui
```

- **TAA** (compute, HDR): reads `hdr()`, depth, velocity, history; writes `history[cur]`, then copies it into `hdr()` so every later pass is unchanged (one extra copy; swapping handles would touch every pass). Resolve = YCoCg variance clipping (gamma 1.25), 5-tap Catmull-Rom history read, `1/(1+luma)` weighted blend, NaN/Inf sanitising, all as in the standalone.
- **Ghosting rejection** (beyond the standalone's color clipping):
  1. *Depth disocclusion:* a copy of the previous frame's depth is kept; the reprojected previous depth is compared with the current depth and a mismatch (relative tolerance) gives the history zero weight.
  2. *Velocity dilation:* each pixel uses the velocity of the closest-depth pixel in its 3x3 neighbourhood, so moving-object edges do not smear.
  3. *Motion-adaptive feedback:* feedback falls as pixel speed rises, on top of the clip-distance reduction (`taa_feedback`).
  4. Off-screen reprojection and a dropped history (resize, camera cut, reset request) output the current frame alone.
- **Motion blur** (compute, HDR): 16 taps along the pixel's velocity (read from the velocity target, not rebuilt from the camera), scaled from per-frame to the exposure time using the render thread's frame delta, clamped to `MaxBlurPixels`, per-pixel interleaved-gradient jitter; reads `hdr()`, writes a scratch target, copies back. Under 1 pixel of motion it is the identity. Runs after TAA, on the resolved image.
- **Targets** in `ViewportTarget`: `velocity()` RG16F, `taa_history(i)` x2 RGBA16F, `taa_depth_history()` R32F, `post_scratch()` RGBA16F. They follow the viewport generation counter; the existing wait-for-idle on recreation covers them. New passes use their own binding layouts and avoid the frame-set registers (t1..t9, s10/s11, b0/b1) and material-set registers (t10..t13, s14, t15 for AO); any pass using the frame layout still calls `setPushConstants`.
- **FXAA and TAA** stay two selectable options of one anti-aliasing choice (Off / FXAA / TAA), exclusive: FXAA after a TAA resolve only blurs.

## Settings

`RenderQuality` (`render_quality.json`, project-wide):
- `taa` (bool, default off). `sanitized()` clears `fxaa` when `taa` is set. Missing keys load defaults.

`SceneSettings` (per scene; `.wscene` `settings` block and binary payload, `sanitized()`, `settings_revision`):
- `Taa { feedback }` (0.8..0.98, default 0.9).
- `MotionBlur { enabled (default off), exposureTime (0.0005..0.1, default 0.012 s), strength (0..4, default 1), maxBlurPixels (1..100, default 32) }`.
- The binary payload gains a mandatory block, as 2c-ii's post block did: the payload version is bumped, scene smoke requires every truncation to be rejected, old `.wscene` files load the defaults.

## Panels

One new panel, **Post effects**, in `editor/src/panels/`, with a tab per effect. Slice A adds the **Temporal AA** tab (feedback, a history reset button, a read-only note and a pointer to the Quality panel when TAA is not enabled) and the **Motion blur** tab. Slices B and C add their tabs (fog, SSR, color LUT, user effects) to the same panel, so there are no single-purpose panels for new effects. The existing Exposure, Bloom, Tonemap and Ambient occlusion panels are left as they are; moving them into tabs is a separate cleanup that this spec does not do.

The Quality panel's FXAA checkbox becomes an anti-aliasing selector: Off / FXAA / TAA.

Wiring is in `editor_module.cpp`, as for the existing panels. A debug option in the Post effects panel shows the velocity target.

## Testing

Checks under `engine/src/renderer/test/` via `register_render_check`, hooked through `render_test.h/.cpp`, run with `--render-test --api=<vulkan|d3d11|d3d12> --gpu-validation`, using `SceneTester`. Every check has a negative control (break a line, confirm failure, restore).

- **Velocity:** moving object, moving camera and a still scene give the expected values (exactly zero when still); zero in the frame after an object stops; sky pixels get the rotation velocity; masked materials write velocity; the transparent velocity pass writes where alpha >= 0.5 only; the `PreviousClipPosition` hook is used.
- **Jitter/culling:** shadows, picking and culling are identical with TAA on and off; sub-pixel jitter does not move picks.
- **TAA:** a still camera converges and then does not change between frames; a hard edge ends with intermediate coverage; a moving object across a contrasting background leaves no trail above a residual threshold after N frames (negative control: disable the depth check, the check must fail); a revealed background is not blended with the object that left it; history is dropped on resize and camera cut; NaN input does not enter the history.
- **Motion blur:** identity with zero velocity; blur only along the velocity direction; length clamp; independent of frame rate for a fixed exposure time.
- **Defaults:** TAA off and motion blur off are bit-identical to the current baseline; existing prepass checks are re-baselined for the extra target.
- **Settings:** scene save/load round trip, truncation rejection, legacy scene files, `render_quality.json` including `taa` clearing `fxaa`.
- Cross-backend images must match (mean < 3, max <= 40). After editing a shader, run `wiwa_editor.exe --render-smoke 100 --api=d3d11` before `--render-test`.

## Assumptions

- No skinning exists in the runtime today, so instance motion is the world matrix.
- The render thread never calls `Engine::get_module` or touches the ECS; settings store `AssetRef`, never `Handle<T>`.

## Conventions

No `std::cout` (use `WI_LOG`), premake only, no `WI_SHIPPING` for editor/game decisions, `near`/`far` are Windows macros. Never commit `PROMPT.md` or `docs/superpowers/`; commit code with explicit staging and no Claude attribution.
