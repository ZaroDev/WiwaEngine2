# Decals - design

Sub-project 1 of "decals and VFX" (VFX/particles is a separate spec, built after this one on the same extractor and pass path). Status: design, not implemented.

## Goal

General-purpose projected decals: a `Decal` component projects a `Material` asset onto opaque scene geometry, changing base color, normal, ORM and emissive. Usable from the editor, Lua and prefabs. Works on Vulkan, D3D11 and D3D12, with TAA/velocity unchanged.

## Non-goals (v1)

Decals on transparent surfaces; surface-shader materials on decals; animated/flipbook decals; decals in offscreen views (unless views.md makes it free); AO or shadows affected by decals; ghost-free moving decals under TAA (a decal attached to a moving entity may ghost slightly).

## Asset and component

No new asset type: a decal uses a `Material` (`.wmat`): its `baseColor`, `normal`, `orm`, `emissive` textures and factors and its alpha. Alpha mode, double-sided and the surface shader of the material are ignored. `AssetType` is unchanged.

`Decal` (`WI_COMPONENT`/`WI_FIELD`; serializer, inspector, overrides, clone, Lua and stubs come from reflection):

| Field | Meaning |
| --- | --- |
| `Material` | `AssetRef` to the `.wmat` |
| `Size` | box extents in local space; projection along local -Z; the transform's rotation and scale apply |
| `Opacity` | 0..1 |
| `Channels` | four toggles (color, normal, ORM, emissive) |
| `NormalAngleFadeStart/End` | degrees; surfaces facing away from the projection fade out |
| `EdgeFade` | fraction of the box faded at its sides |
| `SortOrder` | int; higher draws later and wins overlaps |
| `FadeInTime`, `Lifetime`, `FadeOutTime` | seconds; `Lifetime` 0 = permanent |

`MeshRenderer.ReceivesDecals` (default true) lets an object opt out.

## Scene to render thread

`SceneExtractor` compares `Decal` + `Transform` with what it last sent, keyed by `EntityGuid`, and emits `UpsertDecal`/`RemoveDecal` (world matrix, material `AssetRef`, fields, current opacity). `RenderScene` keeps decals in a flat array by stable slot with box bounds. The render thread never touches the ECS. Materials use the existing per-material binding set (`t10..t13`, `s14`) from `GpuResourceCache`; until textures are Ready the fallbacks are used.

Lifetime: fade and expiry run on the main thread in a small system after the scripts, only while the game clock plays (edit mode shows full opacity). It computes the current opacity and destroys expired entities. Editor Play runs on a clone, so Stop discards spawned decals.

## Render pass

Frame order: `... Light culling -> AO -> Decals -> Forward opaque -> ...`. AO and the prepass keep geometric normals.

Each visible decal is a unit cube drawn from `SV_VertexID` (no vertex buffer), front faces culled, no depth test (camera inside a box works). The pixel shader:

1. reads prepass depth and rebuilds the world position with the **jittered** inverse view-projection;
2. transforms it to decal space and discards outside the box;
3. fades by edge distance, opacity/lifetime and the angle between the prepass normal and the projection axis;
4. samples the material textures through the material set;
5. blends its normal map with the prepass normal by reoriented normal mapping.

Decals are CPU-frustum-culled and sorted by `SortOrder`, then slot; one draw per decal; cap 512 visible per view (overflow dropped with one log).

Targets (in `ViewportTarget`, following the viewport generation counter, existing only while the view has visible decals; 16 B/pixel, about 59 MB at 1440p):

| Target | Format | Content / blend |
| --- | --- | --- |
| Albedo | `RGBA8_SRGB` | premultiplied color, alpha = coverage; `ONE, INV_SRC_ALPHA` |
| Normal | `RGBA8` | world normal, octahedral, premultiplied, alpha = weight; `ONE, INV_SRC_ALPHA` |
| ORM | `RGBA8` | premultiplied occlusion/roughness/metallic, alpha = weight; `ONE, INV_SRC_ALPHA` |
| Emissive | `R11G11B10F` | additive, premultiplied by decal alpha; `ONE, ONE` |

Forward integration: right after `Surface()` fills `SurfaceOutput` (built-in and custom-shader materials alike) the shader samples the four targets at its screen position: `albedo = albedo*(1-a) + decal.rgb`; the shading normal is lerped toward the decal normal by its weight; occlusion/roughness/metallic are lerped by the ORM weight; `emissive += decal.emissive`. Binding: the existing AO set (set 2, bound in every pass with stand-ins) gains `t24..t27` and a clamp sampler (not a new set: the skin set must stay last). With no decals the stand-ins have weight 0 (no shader branch). Blended items bind the stand-ins. With zero decals output must be bit-identical to before.

## Opt-out (`ReceivesDecals`)

The depth prepass writes stencil bit 4 (value 4) for non-receivers, with a stencil write mask of 4 and a per-draw (dynamic) stencil ref. The selection outline already owns bits 0 and 1 (ref 3, mask 3) and runs later, so bit 4 is free; the depth prepass clears the stencil to 0 every frame. The decal pixel shader samples the stencil plane (`ViewportTarget::STENCIL_VIEW_FORMAT`, `X32G8_UINT`, proven on all three APIs by `stencil_checks.cpp`) next to the depth plane and discards pixels with the bit set. No read-only depth-stencil binding is needed, so the spike of phase 1 is dropped.

## Editor, Lua, demo

- Inspector from reflection with a material picker; Add Component entry. Selected decal draws its box and projection arrow through `DebugDraw`. Pick uses a camera-facing icon quad like lights.
- Lua: component via reflection; helper `Decal.spawn{ material, position, normal, size, lifetime, fade_in, fade_out, sort_order }` orienting the box along `-normal` (final names to match `docs/scripting.md` conventions).
- Demo (`scripts/gen_demo_scene.py`): procedural CC0-safe textures from a script (cracks, bullet holes with normal map, emissive rune circle, footprint); rune circle on the floor, bullet holes on a wall, a scorch mark crossing two meshes; a Lua script spawning fading footprints on the warrior's footstep events; the warrior has `ReceivesDecals` off.

## Files (new unless noted)

- `engine/src/scene`: `Decal` in `components.h`, registration in `scene_components.cpp` (edit), `MeshRenderer.ReceivesDecals` (edit), lifetime system.
- `engine/src/renderer`: `passes/decal_pass.*`, `scene/scene_extractor.*` and `scene/render_scene.*` (edit), `viewport_target.*` (edit), `render_module.cpp` (edit, registration), `test/decal_checks.cpp`.
- `editor/engine/shaders`: `decal.hlsl`; edits to the forward shader/`scene_common.hlsli` and the prepass (stencil bit).
- `engine/src/scripting`: `Decal.spawn` binding; `docs/scripting.md`.
- `editor`: inspector/gizmo hooks; smoke additions.
- `scripts`: decal texture generator; `gen_demo_scene.py` (edit).
- Re-run the premake generator after adding files.

## Phasing

1. (Spike dropped: stencil sampling already works, see Opt-out.)
2. Component, `ReceivesDecals`, extractor and `RenderScene` plumbing, scene/clone/prefab round trips (`--scene-smoke`).
3. Decal pass and targets with the albedo channel only; render tests incl. zero-decal bit-identity.
4. Normal, ORM, emissive channels; angle/edge fade; ordering; opt-out.
5. Lifetime system, Lua `Decal.spawn`, `--script-smoke`.
6. Editor gizmo/picking, demo content, `--demo-smoke`.
7. Docs: new `decals.md` spec; update `rendering.md` (frame order, binding table, targets), `scene.md`, `scripting.md`, the `CLAUDE.md` area table.

## Testing

All smokes need a negative control (see CLAUDE.md). `--render-test` (`decal_checks.cpp`): pixels change only inside the box; angle fade; `SortOrder` overlap; normal override changes lighting; emissive adds light; non-receiver unchanged; zero decals bit-identical; camera inside box; cross-backend match within the current tolerances. `--scene-smoke`: field round trips, prefab overrides, clone. `--script-smoke`: `Decal.spawn`, expiry, Stop discards. `--demo-smoke`: expected decal count, footsteps spawn footprints. After shader edits: `--render-smoke 100 --api=d3d11`, then `--render-test`, with `--gpu-validation` on all three APIs.
