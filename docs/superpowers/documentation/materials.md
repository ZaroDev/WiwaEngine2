# Materials and surface shaders

Code: `editor/src/asset_pipeline/{material_file,surface_shader_parser}.*` and `importers/{material,surface_shader}_importer.cpp`; renderer side `renderer/resources/{gpu_material.h,surface_shader_cache.*}`; contract `editor/engine/shaders/surface_contract.hlsli`. Verified by `--asset-smoke`, `--scene-smoke`, `--render-test` (`custom_material_draws_with_its_shader` and `custom_material_casts_shadow_from_its_shader` render `editor/engine/shaders/surface_check.wshader` and read pixels back).

## Files

- **`.wmat`** - JSON, `"version": 1`: `name`; PBR block (`baseColor`, `metallic`, `roughness`, `emissive`, `alpha` = `opaque|mask|blend`, `alphaCutoff`, `doubleSided`, clearcoat/sheen/transmission - stored, not shaded); `textures` (`baseColor`, `normal`, `orm`, `emissive`: asset path or guid); optional `shader` (a `.wshader`) and `params` (name to value; textures by asset reference). Unknown keys are kept on save; missing keys take `MaterialData` defaults.
- **`.wshader`** - an HLSL surface shader (contract below). Plain `.hlsl` always means an engine shader.

`MeshRenderer.Materials` is a list of Material assets. Models import their materials as editable files: for each used material the model importer writes `<ModelDir>/<ModelName>_materials/<name>.wmat` if absent (unique, filename-safe) and the prefab/mesh references that file. Existing files are never overwritten (user edits survive re-import) and a material removed from the model keeps its file. Old `material/N` sub-assets are replaced by extracted files on the next re-import.

## Surface shader contract

```hlsl
struct MaterialParams
{
    float  Wobble;   // range(0,2) default(0.5)
    float4 Tint;     // color default(1,0.5,0,1)
    int    Mode;     // range(0,3) default(0)
    bool   Flip;     // default(false)
};
Texture2D Mask;      // @param   (up to 4, sampled with the material sampler, bound t16..t19)

void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o);   // required
float3 VertexOffset(VertexInput v, MaterialParams p);                     // optional, object space
```

- `SurfaceInput`: world position and normal, tangent frame, uv, view direction, front-face flag, time. `SurfaceOutput`: albedo, tangent-space normal, metallic, roughness, emissive, alpha, occlusion, **pre-filled** with what the built-in PBR path computes from the material's own fields and textures, so a shader changes only what it wants. Lighting, shadows, AO, clustered lights, velocity and pick/outline stay the engine's.
- Parameter types: `float`, `float2/3/4`, `int`, `bool`, `color` (annotation on `float3/4`). Annotations are `//` comments after the member: `range(min,max)`, `default(v)`, `color`, `label("text")`; other comments are prose. One parameter block of at most 256 bytes (HLSL packing), at most 4 user textures. Non-finite defaults are rejected.
- Errors (parse, annotation, compile) fail the import with line numbers; the previous cooked version stays in use.
- The contract file is versioned; engine include changes ripple into user shaders because variants are compiled by including the user file into the engine shader with the hook defined.

## Importers

- `MaterialImporter` resolves textures and the shader as dependencies and emits `MaterialData` extended with `Shader` (the `.wshader` reference), a parameter byte block and table (name, type, offset). Defaults are written for unset parameters. A material whose shader is missing or failed is still emitted; the renderer shows magenta.
- `SurfaceShaderImporter` tokenizes only the `MaterialParams` struct and `Texture2D ... // @param` lines (the rest goes to the compiler untouched), generates the constant-block and texture declarations, and compiles one variant per pass that draws materials - forward (opaque/mask and blend), depth/normal/velocity prepass (plus masked), transparent velocity, shadow - for DXBC and SPIR-V. Emits a `SurfaceShader` asset: variant binaries, parameter table, texture slot names, layout hash.

## Renderer

- `GpuMaterialRecord` stays 64 bytes; custom materials add a shader id and parameter offset, and their parameter bytes live in one `ByteAddressBuffer` (`t21`, records `t20`).
- A `DrawItem` carries the material's own `Slot`, its surface shader index (`GpuResourceCache::get_material_shader`, 0 = built-in, also while the shader loads or has failed) and `Masked`. Items sort by cull mode, then shader (built-in first), then mesh page, then material.
- `scene_pass::draw_items` takes a `SurfaceDraw`: for an item with a shader it asks the pass's `SurfacePipelines` for the pipeline of the shader's variant (forward: `Forward`; depth prepass: `Depth` / `DepthMasked`; transparent velocity: `Velocity`; shadows: `Shadow` / `ShadowMasked`), keyed by (shader, variant, cull) and rebuilt when the shader's own `Generation` moves (a hot reload) or the pass's framebuffer changes. If the pipeline cannot be made the item draws built-in, with one logged error.
- Surface pipelines use the sets frame (0), material (1), AO (2), surface (3: `custom_layout()`, per-material `get_custom_bindings`) and skin (4). The importer defines `SKIN_SET 4` before including the contract, because `scene_common.hlsli` places the skin buffers by it. The AO set is bound in every pass (a stand-in texture when AO is off).
- Shadow casters read the whole frame set, so they bind `SceneRenderData::shadow_frame_bindings()` (the frame set with stand-ins for the shadow maps being drawn into). Their push-constant material is `matrix | (slot << 3)`, see `surface_shadow.hlsl`.
- Every pass that draws materials picks the shader's variant. A failed or still-loading shader draws built-in (default or magenta record, see `compose_record`); one frame of default look on first load.
- Pick and outline use unmodified geometry, so `VertexOffset` motion is neither pickable nor outlined.

## Editor

Material panel (`editor/src/panels/material_panel.*`) follows the browser selection: PBR fields, texture pickers, shader picker, and a parameter section generated from the cooked table (sliders, drag fields, colour pickers, checkboxes, texture pickers) with shader errors shown with line numbers. Edits write the `.wmat` after ~300 ms of quiet; the file watcher re-imports and hot-reloads. Switching shader keeps parameters with the same name and type. The Asset Browser creates materials and surface shaders from templates (`asset_templates.*`) with a naming popup; MeshRenderer slots have a "locate" button.

## Not built

Full pixel-shader replacement (unlit/custom final colour), material instances/overrides, node graphs, previews, shaders for UI/particles/sky, compute materials. User post effects would reuse the annotated-parameter parser.
