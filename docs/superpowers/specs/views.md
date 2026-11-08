# Offscreen views

Code: `engine/src/renderer/view` (`view_types.h`, `per_view.h`, `view_state.h`, `render_view.*`, `view_registry.h`, `readback.h`, `view_camera.h`, `view_host_system.*`), the view parts of `renderer.cpp`, `render_module.cpp` and `scene_render_data.*`, `engine/src/scene/preview_scene.*`, the `ViewHost` component (`scene/components.h`), the `ViewImage` widget (`engine/src/ui`), `editor/src/panels/view_preview_panel.*`. No smoke tests cover it (the owner decided against them); it was checked by hand on Vulkan, D3D11 and D3D12 with `--gpu-validation`.

A view renders a **scene of its own** (a `Scene` that is not the game's) into a texture, every frame or on request. Clients: the editor's View Preview panel, 3D widgets in game UI (`ViewHost` + `ViewImage`), and anything that wants a picture of an asset (thumbnails: the readback exists, the asset browser does not use it yet). The engine lib owns it, so a game uses it too.

## API (main thread)

`RenderModule::create_view(ViewDesc)` returns a move-only `RenderView` handle (invalid, with one warning, past `MAX_VIEWS` = 16 live views including the main one). Destroying the handle destroys the view. The handle keeps a weak token of the module: after shutdown it does nothing.

- `set_scene(Ref<Scene>)`: **weak** reference. Whoever made the scene keeps it alive; a dead scene leaves the view with its last image (one warning).
- `set_camera(RenderCamera)`: `make_orbit_camera` (view_camera.h) builds one. Without a camera the scene's primary `Camera` is used. The aspect is baked into the projection: set the camera again after `resize()`.
- `resize(w, h)`: 0 in either dimension is ignored; sizes clamp to `ViewportTarget::MAX_SIZE`, and the Thumbnail profile caps the longer side at 512 keeping the aspect.
- `request_render()`: for `ViewMode::OneShot` views. A Continuous view draws every frame.
- `imgui_texture_id()`: `FIRST_VIEW_TEXTURE_ID + id`, chosen on the main thread; the render thread puts the view's LDR texture under it. A view that has not drawn yet (or whose target failed) shows a 1x1 stand-in: transparent, or magenta when `failed()`.
- `read_back(callback, premultiplied)`: RGBA8, straight alpha unless asked. The callback runs on the main thread a few frames later and **never runs if the view is destroyed first**. An empty `ImageData` means failure.

## Render thread

`Renderer` owns `m_MainView` (view 0: the Viewport panel, or the window in a game) and `m_ExtraViews`. A `ViewState` is `RenderScene` + `SceneRenderData` + `ViewportTarget` + flags; `get_scene()`, `get_scene_data()` and `get_viewport()` still return view 0's. Commands carry no view id: lifecycle commands (`add_view`, `remove_view`, `resize_view`, `request_readback`) address views by id, and scene deltas are applied to `world.get_view(id)` (null when the view is gone, so a late command does nothing).

The frame loop builds and writes every view that will draw, then runs the whole pass list per view into one command list: **offscreen views first, the main view last**, so ImGui and the game UI sample finished images in the same frame. At most `MAX_VIEWS_PER_FRAME` (4) offscreen views draw per frame: Continuous first, then one-shots, each by the least recently drawn. Only the main view is profiled per pass.

- **Passes are shared; their state is per view.** Pipelines are made once per shader generation (all views have the same target formats). Framebuffers, binding sets and temporal state (TAA history index, exposure) live in `PerView<ViewCache>` keyed by `ViewportTarget::uid()`; a slot starts over when its target is made again. `shadow`, `light_cull`, `ao`, `bloom`, `taa`, `motion_blur`, `exposure`, `tonemap` and `fxaa` do this; the others only build pipelines.
- **Binding layouts are shared**: `SceneRenderData::initialize(device, shareLayoutsFrom)` makes every view borrow the frame, AO and skin layouts of the main view, so a pipeline made with them fits the binding sets of every view. A view must not create its own.
- `SurfacePipelines` holds its framebuffer by handle: its entries are made lazily, long after the view that configured it may be gone.
- `IRenderPass::runs_in_view(flags, isMain)`: game UI, selection outline, debug draw, pick, blit and ImGui are main-only; the others follow the view's `ViewFlags`.
- Shared across views: `GpuResourceCache`, `EnvironmentCache`, `ShaderLibrary`, `UploadQueue`, the device. Every view has its own instance buffer, cluster buffers, shadow maps (made on demand), constants.

## Profiles and flags

`ViewProfile` expands to `ViewFlags` (`view_flags_for`): **Full** (project quality decides), **Preview** (2 cascades, no spot shadows, no AO, no TAA or motion blur, manual exposure, bloom and FXAA), **Thumbnail** (no shadows, depth-only prepass, no light culling, no post but tonemap, 512 px cap). `ViewDesc::FlagOverride` replaces them. Flags are applied in two places: `Renderer::effective_quality` (TAA and FXAA: for an offscreen view the **profile** decides FXAA even when the project has it off, TAA only when the project has it on and the profile allows it) and `SceneRenderData::build` (AO, bloom, auto exposure and motion blur are switched off in the copy of the scene settings; shadows are cut down; light culling can go). A view does not inherit the game's `SceneSettings`: its own `Scene` carries them. `make_preview_scene()` is a neutral studio (key, fill and rim directional lights, solid background, manual exposure); `PreviewModel` shows a prefab in it once the prefab has loaded.

## Transparent background

`ViewDesc::TransparentBackground`: the forward pass clears the HDR target to alpha 0 and the sky is skipped; the HDR alpha is **coverage** (opaque and masked surfaces write 1 in `forward.hlsl`/`surface_forward.hlsl`, blended ones their alpha, accumulated "over"); tonemap divides the color by the coverage before the operator and writes **straight alpha** into the LDR target (ImGui and the UI blend with straight alpha); FXAA filters premultiplied and returns straight. Readback premultiplies on request. A view with TAA on logs once and draws its background instead (the history mixes alpha). The main view is never transparent.

## ViewHost and ViewImage

`ViewHost` (fields in `scene_components.cpp`, so the inspector, files, clone and Lua `entity:get("ViewHost")` work) is the game-side entry. `ViewHostSystem` runs from `RenderModule::render_update` on the **active scene** (the edit scene or the play clone; a different scene object starts over): per host it makes a `RenderView` (made again when the profile, `Transparent` or `Continuous` changes), a preview scene and a `PreviewModel`, follows the fields every frame (camera orbit, size, spin about the vertical axis), names the view in `ViewRegistry` and destroys the views of hosts that are gone. A `ViewImage` widget names the host (`ViewName`); `UiModule` resolves the name to a texture id when the draw list is built (`UiLayoutInput::ViewTexture`, one warning per unknown name) and `UiBatch::ViewTexture` makes the UI pass look the id up in the `TextureRegistry` on the render thread. The UI editor canvas does not draw views (no resolver there).

## Users in the editor (`editor/src/asset_editor`)

The Asset Editor preview is one continuous Preview-profile view that exists while the panel is drawn. The thumbnail cache owns one 128x128 transparent Thumbnail-profile **OneShot** view, reused for every thumbnail (one at a time; each waits about eight frames for the pose, the camera and the GPU mesh, and asks again if the first image is empty). Their scenes are registered with `AnimationModule::register_preview_scene` so their animators advance without a game clock.

## Traps

- **Views do not animate yet.** The preview scene is extracted without palettes and `AnimationModule` is not ticked for it: skinned meshes show their bind pose. `AnimationModule` keeps its instances per last-seen scene, so ticking two scenes in one frame would reset them every frame; it needs to hold several scenes first.
- `ViewState`'s handles must be released before the device goes; `Renderer::shutdown` does it extras first, then the main view.
- Removing a view waits for the GPU (`waitForIdle`) and drops its pending readbacks. Resizing a target waits too: avoid resizing every frame.
- A readback is copied the frame its view draws (or, for a one-shot view with nothing requested, at once from its last image) and delivered when the GPU's event query passes; at most 4 are in flight, the rest wait in order.
- The Performance panel's pass timings are the main view's only.
- `RenderModule` updates the world transforms of a view's scene before extracting it, so whoever owns the scene does not have to.

## Out of scope

Debug draw, outline and picking in offscreen views; a view of the game's own scene; animation in views; the thumbnail disk cache and asset browser use; TAA with a transparent background; MSAA; ground plane in `PreviewSceneOptions` (no plane mesh in the engine assets).
