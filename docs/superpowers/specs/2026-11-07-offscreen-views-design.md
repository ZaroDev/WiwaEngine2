# Offscreen scene views (sub-project A)

First of three sub-projects: **A** offscreen views (this doc), **B** animation asset model (skeleton, clip, skinned mesh as standalone assets with editors and previews), **C** animation graph (node asset, state machines, editor, Lua). B and C use A for their previews. After implementation, fold this into `views.md` and correct `rendering.md`.

## Goal

A general engine feature: render an independent 3D scene into a texture, on demand or every frame. Clients: the animation preview panel, asset thumbnails, and 3D widgets in game UI (a character in an inventory menu). Lives in the engine lib, so the game can use it. Editor and game use the same API.

## Decisions

- **Own scene per view.** A view owns a separate `Scene` (entities, lights, settings, camera). It never shows the game world.
- **Reuse the existing passes** (Approach 1). Per-view state is bundled in a `ViewState`; the renderer runs the pass list once per active view. No second `Renderer`, no cut-down pipeline.
- **On-demand rendering:** `Continuous` (every frame) or `OneShot` (once per `request_render()`). A view that is neither costs nothing.
- **Three outputs:** ImGui texture id, game-UI texture, async CPU readback.
- **Per-view quality profile** and **per-view transparent background.**
- **Game-side entry:** a `ViewHost` component. No Lua-created views.
- **Verification:** Release builds, manual checks. No smoke tests and no new `--render-test` checks (user decision).

## Main-thread API

```cpp
enum class ViewProfile { Full, Preview, Thumbnail };
enum class ViewMode { Continuous, OneShot };

struct ViewDesc {
    u32 Width, Height;
    ViewProfile Profile = ViewProfile::Preview;  // expands to flags; each flag can be overridden
    bool TransparentBackground = false;
    ViewMode Mode = ViewMode::Continuous;
};

class RenderView {                 // RAII, moveable, main thread
    void set_scene(Scene*);
    void set_camera(const RenderCamera&);
    void resize(u32 w, u32 h);
    void request_render();         // OneShot
    u32 imgui_texture_id() const;
    ViewId id() const;             // what a ViewImage UI widget stores
    void read_back(std::function<void(ImageData)>, bool premultiplied = false);
    bool failed() const;
};
RenderView RenderModule::create_view(const ViewDesc&);   // invalid handle when the cap is hit
```

- Each view has its own `SceneExtractor`. `RenderModule` runs it once per active view per frame and appends **view-tagged** deltas to the batch.
- Scene-bound commands carry a `ViewId`. **View 0 is the main viewport**, so existing paths keep working. Editor-only commands (camera override, selection, pick) stay main-only.
- Destroying the handle sends `DestroyView`; the render thread waits for the GPU, then frees the targets.
- Cap: 16 live views. Past it, `create_view` logs and returns an invalid handle.
- A view with a pending readback that is destroyed cancels the callback.

## Render-thread side

- `ViewState` (render thread only): `RenderScene` (instances, lights, camera, skin palettes and previous palettes), `SceneRenderData` (draw lists, cluster and shadow buffers), `ViewportTarget`, and the temporal state passes keep today (exposure buffer, previous camera, history-reset flag, TAA history).
- `Renderer::m_Scene`, `m_SceneData`, `m_Viewport` become `m_Views[0]`. The existing accessors (`get_scene`, `get_scene_data`, `get_viewport`) stay and return view 0, so commands and render checks compile unchanged.
- **Shared by all views:** `GpuResourceCache`, `EnvironmentCache`, `ShaderLibrary`, `UploadQueue`, device, pipelines. Meshes, textures and materials are uploaded once.
- `FrameDescription` gains `ViewState* View`. Passes that kept state in members (TAA, exposure, light culling, shadow atlas) move it to `ViewState`. The implementation plan must audit every pass for such state.
- Each pass declares a `PassMask` of profiles it runs in. Main-only passes (game UI, selection outline, debug draw, pick, blit, ImGui) skip other views.
- **Frame loop** (one command list): (1) each active non-main view in id order, (2) the main view, (3) the main-only tail, (4) queue readbacks of one-shot views that rendered. Views render before the main view, so panels and UI widgets sample a finished texture with no extra frame of latency.
- **Budget:** a few views rendered per frame (continuous first, then one-shots oldest first); the rest wait for a later frame.
- `TextureRegistry` gets dynamic ids for views next to `VIEWPORT_TEXTURE_ID`, freed on `DestroyView`. `resize()` repoints the entry, so the id stays valid.

## Outputs

1. **ImGui:** `imgui_texture_id()`; a never-rendered view shows a cleared texture.
2. **Game UI:** a `ViewImage` widget (new `UiWidgetType`) stores a `ViewId`; the draw list item carries it and `UiRenderPass` resolves it to the view's LDR texture on the render thread. A transparent view composites through the normal UI blend. The game owns the view through a **`ViewHost` component** (`WI_COMPONENT`/`WI_FIELD`: preview scene or asset to show, camera, profile, transparent flag, mode) holding the `RenderView`. Inspector, serialization, clone and Lua come from the reflection. Lua can change the shown model or camera and request a render.
3. **Readback:** copy the LDR target to a staging texture; when the fence passes (typically 1 to 3 frames), send `ImageData` (RGBA8) to main and run the callback there. A cap of in-flight readbacks; extras queue. Straight alpha by default, premultiplied on request.

Disk thumbnail caching and asset browser integration are project B.

## Profiles

| Flag | Full | Preview | Thumbnail |
|---|---|---|---|
| Shadows | 4 cascades + spot | 1 to 2 cascades | off (single fill light) |
| Depth/normal/velocity prepass | yes | yes | depth only |
| Light culling | yes | yes | skipped |
| AO | per settings | off | off |
| TAA, motion blur | per settings | off | off |
| Bloom | per settings | per settings | off |
| Exposure | per settings | manual EV | manual EV |
| Tonemap, FXAA | yes | yes | tonemap only |
| Resolution cap | none | none | 512 px |

A view does not inherit the game's `SceneSettings`; its own `Scene` carries them. `Scene::make_preview_scene()` builds a neutral studio: three-point light rig, gradient or solid background, optional ground plane. Animation preview and thumbnails use it.

## Transparent background

Sky pass and clear colour write alpha 0. The forward pass writes coverage (opaque 1, blended its alpha). Tonemap writes premultiplied alpha; FXAA keeps the alpha channel. **Full profile with TAA and a transparent background is unsupported in v1**: it logs and drops alpha.

## Temporal state

Previous camera and TAA history are per view and reset on resize, scene reset or camera cut. A one-shot view always renders without history.

## Errors and traps

- Target creation fails: `failed()` is true, the view shows magenta, no retry until `resize()`.
- Shutdown order: each view's GPU handles reset before the device goes (Vulkan and D3D12 crash otherwise), as in `SceneRenderData::shutdown`.
- Non-main views have no debug draw, outline or picking.
- Resource transitions between views rely on NVRHI state tracking; confirm with `--gpu-validation` during manual runs.

## Manual verification (no smoke tests)

- A permanent editor **View Preview** panel: a view over a preview scene, orbit camera, profile selector, ImGui output, and a "Save PNG" button that exercises readback.
- `demo.wscene` (via `scripts/gen_demo_scene.py`): a `ViewHost` plus a `ViewImage` widget showing the warrior rotating in a corner menu.
- Build `Release`; run on Vulkan and D3D11 by hand. Use Debug only when something needs debugging.

## Docs to update with the code

New `views.md`; fix `rendering.md` (single viewport, frame loop, texture registry); `docs/scripting.md` (`ViewHost` Lua); CLAUDE.md area table row for Views.

## Out of scope

Debug draw, outline and picking in non-main views; views of the main game scene; disk thumbnail cache and asset browser (B); TAA with transparent backgrounds; MSAA; stereo; compute skinning changes.
