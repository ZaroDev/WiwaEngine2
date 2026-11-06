# Animation

Code: `engine/src/animation` (poses, nodes, `AnimatorInstance`, `AnimationModule`), `engine/src/scripting/bindings/bind_animation.cpp`, the skinning parts of `engine/src/renderer`, `editor/src/animation` (panel, edit model, smoke tests), importers in `editor/src/asset_pipeline/importers/model_skeleton.*`. User API: `docs/scripting.md`. Verified by `--anim-smoke`, `--anim-panel-smoke`, `--render-test` (`skinning_checks.cpp`) and the fox in `--demo-smoke`.

Part 1 of 5 (foundation). Later parts are the animation graph, root motion, IK and ragdoll; this area only keeps them possible (see the end).

## Assets

- `SkeletonData`: `Joints` (prefab node indices), `InverseBind`, `JointNames`, `Parents` (joint index, -1 for a root; **a parent always comes before its children**), `RestTranslation/Rotation/Scale`. The model importer fills them (`finalize_model_skeletons`): it sorts the joints parent first and **remaps the joint indices of every mesh vertex** it skins. The rest pose of a root joint includes the nodes above it, so poses are in the space of the model root (the entity with the `Animator`). More than 256 joints is a warning (the palette cap).
- `AnimationClipData`: channels keyed by prefab node index, plus `Events` (`{time, name}`, sorted). Events are the `animationEvents` list of the model's import settings (`{clip, time, name}`); the Animation panel writes it. Editing it re-imports the model. **`.meta` files are git-ignored in this repo**, so events live in the local `.meta` only.
- `WASSET_VERSION` is 2. The import cache entry stores `wassetVersion`, so a bump re-cooks everything (the importer version alone would not).
- A **ClipBinding** (`bind_clip`) maps each channel to a joint by node; channels for nodes outside the skeleton are left out with one warning per clip.

## Components

`Animator` (`Skeleton`, `Clips`, `Clip`, `PlayOnStart`, `Speed`, `Enabled`, `BoundsPadding`), `AnimationSocket` (`Joint`, `OffsetPosition`, `OffsetRotation`), `SkinBinding` (`Skeleton`). All reflected. The prefab expander adds `Animator` to the root of an instance of a model with animations (unless the scene already has one) and `SkinBinding` to every skinned node. `Animator.Graph` is not there yet: part 2 appends it.

Runtime state is not a component: `AnimationModule` (priority 10, after the scripts) keeps an `AnimatorInstance` per Animator **of the scene it last saw** (another scene object, such as the play-mode clone, starts with none; an asset reload drops them all). Instances exist while the game clock plays, and as editor previews. `AnimationModule::tick(scene, dt, playing)` is the testable seam; `update()` calls it. Scripts run before it, so `acquire()` makes an instance on demand for them.

## Evaluation

`Pose` = local TRS per joint. Nodes (`pose_nodes.h`): `ClipNode` (time, loop/once, per-channel key cursor, events), `BlendNode` (lerp/nlerp along the shortest path, optional ramp), `AdditiveNode` (clip minus its first frame), `MaskNode` (per-joint weights), `RestNode`. `advance(dt, weight, events)` moves state and emits events; `evaluate(pose)` is a function of the node's own state. An event fires only for a clip whose effective weight is above 0.5, so the two sides of a crossfade do not both fire a footstep.

`AnimatorInstance`: `play` replaces the root, `crossfade` wraps it as `Blend(old, new)` and ramps, `stop` fades to `RestNode`; a finished blend collapses to its child, and a crossfade started with 24 nodes or more settles the older fades first, so the tree stays bounded. Layers (`add_layer`) are kept beside the base: masked (override) or additive (one clip). A command that needs a clip that has not loaded waits, in order, and runs the frame it can; a name that is unknown once every clip has loaded is logged and dropped.

Palette = `model[j] * inverseBind[j]`, three float4 rows per joint (`JointMatrix`). Sockets are written after evaluation (`AnimationSocket`'s `Transform` from the joint, relative to its parent).

## Skinning and rendering

- A mesh keeps `SkinVertices` (four joints and weights) in a **per-page skin buffer** parallel to the page's vertex buffer, created when the first skinned mesh lands on the page, uploaded with the mesh. Rigid meshes cost nothing.
- Palettes travel main to render in the same `SceneExtractor::extract` as the world matrix (`SceneDelta::PaletteUpdates`, sent when a pose differs from what was sent; an empty one makes the instance rigid again). `RenderScene` keeps `PrevPalette` and rolls it in `end_frame`, like `PrevWorld`: the render thread keeps the previous one, not main. A skinned instance's world matrix is the **Animator's**, not the mesh node's, and its culling bounds grow by `Animator::BoundsPadding`.
- `GpuInstance` has a `Skin` row (palette rows now and before; `NO_PALETTE` = rigid). Vertex shaders skin before the world matrix with `SkinPoint`/`SkinNormal` (`scene_common.hlsli`); `PreviousClipPosition` skins with the previous palette. A user surface shader's `VertexOffset` applies before skinning. Non-uniform joint scale is not supported (normals use the upper 3x3). A palette shorter than the mesh's joint count is ignored (drawn rigid) so the shader never reads past it.
- **Edit mode:** a skinned mesh with no palette is drawn unskinned at the Animator's world, which is exactly the rest pose.

## Traps

- **The skin data cannot live in the frame set.** Its registers span 1..9 and the material layout sits in t10..t13 (D3D12 wants each layout's range to be contiguous), and Vulkan's binding 0 is already `b0`. Palettes (`t22`) and skin vertices (`t23`) are their own **skin set, always the last set** of every pass that draws meshes; each mesh shader defines `SKIN_SET` (its number on Vulkan: the count of layouts before it) before including `scene_common.hlsli`. A new pass that draws meshes must append `data.skin_layout()` to its layouts (`scene_pass::draw_items` binds the set).
- **`SV_VertexID` differs by API:** Vulkan includes the base vertex, D3D does not. The base vertex is the third push constant of a mesh draw (`DrawConstants.BaseVertex`, 12 bytes) and `SkinVertexIndex` adds it on D3D.
- Shutdown order: the new handles in `SceneRenderData::shutdown` must be reset before the device goes (Vulkan and D3D12 crash otherwise).
- `ScriptingModule` reads the animation module when it starts, so `Engine` registers `AnimationModule` before it.
- Events reach scripts one frame late, like UI events; `joint_world` reads the previous evaluation.

## Editor

`AnimationPanel`: for the selected entity with an `Animator`, a preview of a clip in the viewport (the module's preview instance: play, pause, scrub, speed, loop; ends when the selection changes or the game plays, see `preview_continues`) and a timeline to add (click), drag (one undo step on release), rename (rows) and delete events, through `AnimationEditModel` (ImGui-free, undo, validation). Save writes `animationEvents` to the model's `.meta`. The inspector shows the socket joint as a drop-down of the skeleton's joints. "Show skeleton" draws the joints with `DebugDraw`.

## Checks

`--anim-smoke` (import, sampling, nodes, events, module, previews, layers, sockets, extractor, Lua API, edit model); `--anim-panel-smoke` (the panel for 130 frames with ImGui asserts on, events saved into a `.meta`); `--render-test` (`palette_previous_follows_end_frame`, `skinned_quad_follows_palette` at a nonzero base vertex, `skinned_without_palette_equals_rigid`, `skinned_palette_too_short_is_ignored`, `skinned_velocity_uses_previous_palette` with a no-motion control); `--demo-smoke` (the fox).

## Hooks for later parts

Part 2: the `Graph` field, a state-machine pose node, `anim:set`. Part 3: `Pose` gains a root delta. Part 4: IK nodes after the output node; foot IK needs the physics snapshot of the same frame. Part 5: `Pose` converts to `JPH::SkeletonPose`. Job evaluation: `evaluate` writes only the instance's own buffers. Compute skinning: the render thread only knows "this instance's palette starts at row N".

## Out of scope

Retargeting, morph target rendering, animation LOD, job evaluation, compute skinning, non-uniform joint scale, per-frame pose-derived bounds, procedural pose writes from Lua, graph, root motion, IK, ragdoll.
