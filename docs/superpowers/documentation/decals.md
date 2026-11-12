# Decals

Code: `Decal` in `engine/src/scene/components.h`, lifetime in `engine/src/scene/decal_lifetime.*` and `decal_module.*`, the pass in `engine/src/renderer/passes/decal_pass.*`, the targets in `viewport_target.*`, shader `editor/engine/shaders/decal.hlsl` plus `ApplyDecals` in `pbr.hlsli`, Lua `engine/src/scripting/bindings/bind_decal.cpp` (user API: [docs/scripting.md](../../scripting.md)), editor gizmo in `editor/src/gizmo/scene_gizmos.cpp`. Verified by `--render-test` (`tests/src/render/decal_checks.cpp`), `--scene-smoke`, `--script-smoke` and `--demo-smoke`. Design record: [decals-design.md](decals-design.md) (written before the build; this file is what exists).

## What it is

A `Decal` component projects a `Material` (`.wmat`) onto the opaque geometry inside its box, changing base color, normal, ORM and emissive. There is no new asset type: a decal reads the material's four textures, its factors and its base color alpha. The alpha mode, double-sided flag and the surface shader of the material are ignored. Works on Vulkan, D3D11 and D3D12; TAA and velocity are unchanged.

Not supported: decals on blended (transparent) surfaces, surface-shader materials on decals, animated decals, shadows or AO affected by decals. A decal on a moving entity may ghost slightly under TAA.

## Component

`Decal` (`WI_COMPONENT`/`WI_FIELD`: serializer, inspector, prefab overrides, clone, Lua and stubs come from reflection):

| Field | Meaning |
| --- | --- |
| `Material` | `AssetRef` to the `.wmat` |
| `Size` | box extents in local space; it projects along local -Z; the transform's rotation and scale apply |
| `Opacity` | 0..1 |
| `AffectColor/Normal/Orm/Emissive` | which channels the decal writes |
| `NormalAngleFadeStart/End` | degrees (60 / 80) between the surface normal and the projection axis; steeper surfaces fade out |
| `EdgeFade` | fraction of the box faded at its sides (0.1) |
| `SortOrder` | higher draws later and wins overlaps |
| `FadeInTime`, `Lifetime`, `FadeOutTime` | game seconds; `Lifetime` 0 is permanent (it still fades in) |

`MeshRenderer.ReceivesDecals` (default true) lets an object opt out.

## Lifetime

`DecalModule` (priority 11, after the scripts) calls `tick_decals` while the game clock plays: it adds a runtime-only `DecalAge` to the decals that fade or expire, computes the fade factor (`decal_fade`; the opacity sent to the renderer is `Opacity` times it) and destroys the expired ones (`decal_expired`: never with `Lifetime` 0). In edit mode, and in a scene that is not the one being played, a decal shows at full opacity and never expires. Editor Play runs on a clone, so Stop discards decals a script spawned.

## Scene to render thread

`SceneExtractor` compares `Decal` and `Transform` with what it last sent, keyed by `EntityGuid`, and emits `DecalUpserts` / `DecalRemovals` in the `SceneDelta` (world matrix, material `AssetRef`, the fields, the current opacity). `RenderScene` keeps them in a flat array by stable slot. The render thread never touches the ECS. A decal's material uses the same per-material binding set (`t10..t13`, `s14`) as meshes, with the usual fallbacks until the textures are Ready.

## The pass

Registered after AO and before the forward passes (rendering.md): `... Light culling -> AO -> Decals -> Particle simulation -> Forward opaque ...`. It runs in a view when `ViewFlags::Decals` is set (the Preview and Thumbnail profiles switch it off, views.md).

`SceneRenderData::visible_decals()` is CPU-frustum-culled and sorted by `SortOrder`, then slot; the cap is `MAX_VISIBLE_DECALS` = 512 per view (overflow is dropped with one log). Each decal is a unit cube drawn from `SV_VertexID` (no vertex buffer), front faces culled, no depth test, so a camera inside the box works. The pixel shader:

1. reads the prepass depth and rebuilds the world position with the **jittered** inverse view-projection;
2. moves it to decal space and discards outside the box;
3. discards pixels whose stencil has bit 4 set (a mesh with `ReceivesDecals` off);
4. fades by edge distance, opacity and the angle between the prepass normal and the projection axis;
5. samples the material through its binding set and blends the normal by reoriented normal mapping.

Targets (`ViewportTarget`, made on demand by `ensure_decal_targets`, dropped by `release_decal_targets`, and following the viewport generation counter; 16 bytes a pixel, so they exist only while the view has visible decals):

| Target | Format | Content / blend |
| --- | --- | --- |
| Albedo | `RGBA8_SRGB` | premultiplied color, alpha = coverage; `ONE, INV_SRC_ALPHA` |
| Normal | `RGBA8` | octahedral world normal, alpha = weight; `ONE, INV_SRC_ALPHA` |
| ORM | `RGBA8` | premultiplied occlusion, roughness, metallic, alpha = weight; `ONE, INV_SRC_ALPHA` |
| Emissive | `R11G11B10F` | added light; `ONE, ONE` |

**Forward integration.** Where the forward shaders build a surface (built-in and custom-shader materials alike), `ApplyDecals(pixel, ...)` in `pbr.hlsli` loads the four targets at the pixel: `albedo = albedo*(1-a) + decal.rgb`, the shading normal lerps toward the decal normal by its weight, occlusion/roughness/metallic lerp by the ORM weight, `emissive += decal.emissive`. They sit in the AO set (set 2 on Vulkan) as `t25..t28` (AO is `t24`); with no decals 1x1 zero stand-ins are bound, so the shader has no branch and the output is bit-identical to a scene without decals. Blended items bind the stand-ins too.

**Opt-out.** The depth prepass writes stencil bit 4 (`DepthPrepass::DECAL_STENCIL_BIT`) for non-receivers with write mask 4 and a per-draw stencil ref; it clears the stencil every frame. The selection outline owns bits 0 and 1 and runs later. The decal shader samples the stencil plane (`ViewportTarget::STENCIL_VIEW_FORMAT`, `X32G8_UINT`).

## Editor, Lua, demo

- Inspector from reflection with a material picker; Add Component entry. With the **Decals** gizmo toggle on (Viewport Gizmos menu, `gizmo_settings.json`) every decal gets a marker and a pick icon; the selected one also draws its box and its projection arrow (`DebugDraw`).
- Lua: the component through reflection, and `Decal.spawn{ material, position, normal, size, opacity, lifetime, fade_in, fade_out, sort_order, roll, parent, name }` (docs/scripting.md).
- Demo hall: a rune circle on the floor, four bullet holes with a normal map on a column, a scorch mark across the floor and a crate, and `decal_footprints.lua` on the warrior (a footprint at every footstep event; the warrior's meshes have `ReceivesDecals` off). Generated by `scripts/gen_demo_scene.py` (`--decals-only` patches only these into the written scene); `--demo-smoke` counts them.

## Traps

- The decal pass and every forward shader share the AO set: a new binding there must stay clear of `t24..t28`.
- Any shader that reads a projection matrix must use the jittered one for depth reconstruction (rendering.md, temporal chain).
- `--render-test` checks zero decals against bit-identity; a change to `ApplyDecals` must keep that true.
