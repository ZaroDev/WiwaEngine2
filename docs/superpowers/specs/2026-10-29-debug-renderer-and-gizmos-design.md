# Debug renderer, selection outline, picking and gizmos

Part of the renderer integration (`PROMPT.md`). Done so far: asset system, 2a Foundation, 2b-i Scene foundation, 2b-ii Render path, 2c-i Environment and settings, 2c-ii Post-processing. This spec is independent of 2c-iii (TAA, motion blur, fog, SSR, LUT, user effects), which stays pending; nothing here blocks it. The scene-foundation spec deferred "gizmos and viewport picking"; this spec delivers them.

## Goal

An immediate-mode debug renderer in the engine, plus editor tooling built on it: a stencil-based selection outline, GPU ID-buffer picking, a transform gizmo (translate, rotate, scale) and passive gizmos for lights, cameras and bounds. Success: everything works on Vulkan, D3D11 and D3D12, images match across the three, and each piece has a check with a negative control.

## Out of scope

Undo/redo, text or labels in the debug renderer, multi-selection gizmos, Lua bindings (the API stays reflection-friendly for later), CPU mesh raycasting, any 2c-iii effect.

## Debug renderer (engine, main thread)

`DebugDraw` collects shapes on the main thread only (editor panels, systems, later Lua). Nothing on the render thread calls into it.

- **API:** `line`, `box` (AABB and OBB), `sphere`, `circle`, `cone`, `capsule`, `frustum`, `arrow`, `grid`, `axes`, and filled `triangle`/`quad`. Each takes a color, an optional `duration` in seconds (ticked on the main thread when a batch is sealed) and a `DepthMode`: `Tested`, `AlwaysOnTop` or `Faded` (occluded parts drawn dim).
- **Lines:** expanded to screen-space quads in the vertex shader; width is in pixels (1 for debug shapes, 2 to 3 for gizmos).
- **Hand-off:** at batch seal the accumulated vertices move into `RenderBatch` (like `OwnedDrawData` for ImGui). The render thread uploads them to a per-frame dynamic buffer; no locking.
- **Cap:** a per-frame vertex cap; overflow is dropped and logged once with `WI_LOG`.
- **Availability:** works in the editor and in the game in every configuration; never gated on `WI_SHIPPING`. A runtime toggle enables it.

## Passes and targets

Pass order after this work:

```
Shadows -> Depth+Normal prepass -> Light culling -> AO
  -> Forward opaque -> Sky -> Forward blended
  -> Exposure -> Bloom -> Tonemap -> FXAA
  -> Selection outline -> Debug draw -> Blit -> ImGui
  (ID pass: on a click only, see Picking)
```

- **Depth format:** the viewport depth changes from `D32` (`viewport_target.cpp:34`, and the scene depth in `scene_render_data.cpp:52`) to `D32S8` (`D32_S8X24` on D3D, `D32_SFLOAT_S8_UINT` on Vulkan). The depth prepass and forward pipelines are updated for the new format; the stencil is left untouched by them.
- **Selection outline (`SelectionOutlinePass`):** (1) draw the selected entity's meshes with stencil ref 1, stencil write, no color write, depth test off; (2) a fullscreen pass samples the stencil and draws an outline of configurable pixel width on pixels where stencil=1 neighbors stencil=0; where the selection is occluded by scene depth the outline is drawn dimmer. It runs in LDR on the viewport image before the debug pass. The stencil is cleared per frame.
- **`DebugPass`:** line and triangle pipelines, each with a tested and an always-on-top variant, in LDR after the outline so bloom, exposure and tonemap never touch gizmo colors. Depth-tested modes read the scene depth.
- **Bindings:** new registers outside the existing ranges (frame set t1..t9, s10/s11, b0/b1, material set t10..t14, s14); any pass using the frame layout calls `setPushConstants` (zeros if unused). UAV slots avoid u6/u7.
- **Resize:** new targets live in `ViewportTarget` and follow its generation counter; the existing wait-for-idle before recreation covers them.

## Picking (GPU ID buffer)

- A request carries the clicked pixel (viewport coordinates). On the next render frame the render thread renders an `R32_UINT` entity-ID target for that frame only: every instance writes its entity id (+1, 0 = nothing), and light and camera icons are drawn as small camera-facing quads with their entity ids. The clicked pixel is copied to a readback buffer and the result is delivered to the main thread through the existing hand-off; selection lands about 1 to 2 frames after the click.
- The ID target exists only while a pick is pending and follows the viewport size; there is no cost in frames without a click.
- Entities are identified by a stable id carried in `RenderScene` instances; the render thread never touches the ECS or `Engine::get_module`.
- Left click selects (empty space clears the selection); a click that lands on a gizmo handle wins over picking. Fly camera input (right mouse) is unchanged.

## Editor: gizmos (`editor/src/gizmo/`)

- **Transform gizmo:** translate, rotate and scale on the selection; world and local space; snapping with Ctrl; screen-constant size; axis and plane handles; hover highlight. W/E/R switch mode and a viewport toolbar shows it. A drag writes the ECS transform directly (no undo).
- **Passive gizmos:** lights (directional arrow, point range sphere, spot cone), cameras (frustum) and the selection's bounds. A Gizmos overlay menu toggles each category; the settings persist with the editor's other per-user state.
- **Math:** handle hit-testing, rotation angle, snapping and ray/plane/axis closest-point live in pure functions (no GPU, no ImGui) so they are testable.

## Testing

Checks under `engine/src/renderer/test/` via `register_render_check`, hooked through `render_test.h/.cpp`, run with `--render-test --api=<vulkan|d3d11|d3d12> --gpu-validation`, using `SceneTester`:

- **Debug draw:** a line lands on the expected pixels; `Tested` is occluded by nearer geometry, `AlwaysOnTop` is not, `Faded` is dimmer; duration keeps a shape for N frames then drops it; overflow is capped without crashing.
- **Outline:** a selected cube gets an outline at the expected edge pixels with the configured width; an unselected scene is bit-identical to the baseline; an occluded selection outlines dimmer.
- **Depth format:** existing depth, shadow, AO and prepass checks pass unchanged on `D32S8`.
- **Picking:** a click on entity A returns A, on empty space returns none, on an icon quad returns the light, on overlapping entities returns the nearest; the ID target is released after the pick.
- **Gizmo math** (pure, no GPU): ray vs. axis and plane handles, rotation angle, snapping, screen-constant scale.
- **Editor smoke:** a synthetic click selects an entity and a synthetic drag moves it by the expected amount.
- Every task has a negative control (break a line, confirm failure, restore). Cross-backend images must match (mean < 3, max <= 40). After editing a shader run `wiwa_editor.exe --render-smoke 100 --api=d3d11` before `--render-test`.

## Conventions

No `std::cout` (use `WI_LOG`), premake only, no `WI_SHIPPING` for editor/game decisions, `near`/`far` are Windows macros, settings store `AssetRef` not `Handle<T>`. Never commit `PROMPT.md` or `docs/superpowers/`.
