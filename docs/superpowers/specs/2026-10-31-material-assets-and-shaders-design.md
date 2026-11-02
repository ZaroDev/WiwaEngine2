# Material assets and user surface shaders

Date: 2026-10-31. Branch: `renderer-rhi`. Sub-project 1 of 3 (see "Later steps").

## Purpose

Let the user create materials as assets, assign them to mesh renderers, edit them in the editor, and give them a custom shader with
editable parameters. Success means: a user creates a `.wmat`, assigns it in a MeshRenderer's Materials list, changes its values and
shader in a panel, and sees the result in the viewport without restarting; models' own materials are editable the same way; custom
shaders keep lighting, shadows, AO, TAA/motion-blur velocity and Forward+ working.

What the user said: materials as assets assignable to mesh renderers and editable, including the shader; user shaders with user
parameters; parameters declared as a struct with annotations; the extension is `.wshader`; materials of imported models are extracted
to editable files on import; the surface-hook shader model comes first, a full pixel-shader replacement ("both") comes later.

## Existing state (verified)

- `Material` is an `AssetType`; `MaterialData` is PBR factors + four textures (`asset_data.h`). `MeshRenderer.Materials` is already a list of
  Material assets (`scene_components.cpp`).
- Material assets only come from model imports as sub-assets `material/N` (`model_importer.cpp`).
- All materials share one pipeline per cull/blend state: a 64-byte `GpuMaterialRecord` in a structured buffer indexed by `MaterialSlot`, four
  textures in descriptor set 1 (t10..t13, s14). Frame set uses t1..t9, s10/s11, b0/b1; AO uses t15.
- Importers (`IImporter`, `ImportContext`) cook sources to Library with stable sub-asset guids, dependencies and hot reload. `ShaderImporter`
  and `shader_compiler` produce DXBC/SPIR-V per backend. `.hlsl` already means an engine shader.

## Files

- `.wmat` — JSON, versioned (`"version": 1`):
  `name`, PBR block (`baseColor`, `metallic`, `roughness`, `emissive`, `alpha`: `opaque|mask|blend`, `alphaCutoff`, `doubleSided`, `clearcoat`,
  `clearcoatRoughness`, `sheen`, `sheenRoughness`, `transmission`), `textures` (`baseColor`, `normal`, `orm`, `emissive`: asset path or guid),
  optional `shader` (asset path or guid of a `.wshader`) and `params` (name -> value; textures by asset reference).
  Unknown keys are kept on save. Missing keys take the defaults of `MaterialData`.
- `.wshader` — HLSL surface shader (contract below).

## Surface shader contract

```hlsl
struct MaterialParams
{
    float  Wobble;   // range(0,2) default(0.5)
    float4 Tint;     // color default(1,0.5,0,1)
    int    Mode;     // range(0,3) default(0)
    bool   Flip;     // default(false)
};
Texture2D Mask;      // @param   (up to 4 per shader; sampled with the material sampler)

void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o);        // required
float3 VertexOffset(VertexInput v, MaterialParams p);                          // optional, object space
```

- `SurfaceInput`: world position, world normal, tangent frame, uv, view direction, front-face flag, time. `SurfaceOutput`: albedo, tangent-space
  normal, metallic, roughness, emissive, alpha, occlusion, pre-filled with what the built-in PBR path computes from the material's own fields and
  textures, so a shader may change only what it wants. Lighting, shadows, AO, clustered lights, velocity and the pick/outline passes remain
  the engine's.
- Parameter types: `float`, `float2/3/4`, `int`, `bool`, `color` (annotation on `float3/4`). Annotations are `//` comments after the member:
  `range(min,max)`, `default(v)`, `color`, `label("text")`. Packed into one parameter block of at most 256 bytes (HLSL packing rules).
- Errors (parse, annotation, compile) fail the import with line numbers; the previous cooked version stays in use (existing hot-reload rule).

## Importers

1. `MaterialImporter` (`.wmat`): parses and validates the JSON, resolves textures and the shader as dependencies, emits `MaterialData` extended
   with `ShaderRef` (AssetRef) and a parameter byte block + parameter table (name, type, offset). Defaults are written for parameters the file
   does not set. A material whose shader is missing or failed is still emitted with its shader reference; the renderer shows magenta for it.
2. `ShaderSurfaceImporter` (`.wshader`): parses `MaterialParams` and the annotations (small tokenizer over the struct and `Texture2D ... // @param`
   lines only; the rest of the file is passed to the compiler untouched). Generates the constant-block declaration and texture bindings, then
   compiles one variant per pass that draws a material — forward (opaque/mask and blend), depth/normal/velocity prepass (plus masked), transparent
   velocity, shadow — for DXBC and SPIR-V through the existing compiler by including the user file into the engine shader with the hook defined.
   Emits a `SurfaceShader` asset: variant binaries, parameter table, texture slot names, hash of the generated layout.
3. `ModelImporter`: for each used material writes `<ModelDir>/<ModelName>_materials/<material name>.wmat` if it does not exist (name
   made unique and filename-safe), with the model's factors and imported textures, and makes the prefab/mesh reference the `.wmat` main asset.
   Existing files are never overwritten; a material removed from the model keeps its file. Materials already imported as `material/N` sub-assets
   are replaced by extracted files on the next re-import (once, logged). Pak and dependency tracking treat the extracted files as ordinary assets.

## Renderer

- `GpuMaterialRecord` stays 64 bytes. A material with a shader adds `ShaderId` and `ParamOffset` (record gains a second structured buffer of
  custom records, or the existing `Extra` plus flags are reused; the exact encoding is for the plan) and its parameter bytes live in one
  `ByteAddressBuffer` of all custom parameter blocks (t20, own layout together with the user texture slots t16..t19, so no overlap with
  t1..t15).
- Draw items are sorted by pipeline (shader, cull mode, blend) then material. Pipelines are cached per (shader generation, variant, cull, blend,
  viewport generation). A shader change (hot reload) bumps the generation and rebuilds, as `ShaderLibrary` does now.
- Every pass that draws materials selects the variant for the material's shader: depth prepass (including the masked variant that runs `Surface`
  for alpha), forward opaque/blended, transparent velocity, shadow (alpha-tested and vertex-offset aware). Pick and outline passes use the
  unmodified geometry; this limit (vertex offsets are not pickable/outlined) is documented.
- A material with a failed or still-loading shader draws with the existing magenta fallback; the cost is one frame of magenta when a shader
  first loads.
- The render thread never touches the ECS or `Engine::get_module`; scenes keep `AssetRef` to materials; settings never store `Handle<T>`.

## Editor

- Asset browser: "New → Material" and "New → Surface shader" (template with the struct, annotations and a commented example), icons for both, and
  double-click opens the panel (material) or the system editor (shader).
- Material panel (`editor/src/panels/material_panel.*`, category Renderer): follows the selected `.wmat` in the browser. Built-in section
  (all PBR fields, texture slots as asset pickers), shader picker, and a generated parameter section (sliders, drag fields, color pickers,
  checkboxes, texture pickers) from the cooked parameter table. Compile/parse errors of the shader are shown with line numbers. Edits write the
  `.wmat` after ~300 ms of quiet; the existing file watcher reimports and hot reloads. Switching shader keeps values of parameters with the same
  name and type.
- Inspector: a MeshRenderer's Materials list gets a "locate" button per slot that selects the material in the browser.
- Panels are separate from other panels, as for the other renderer settings; no preview sphere in this step.

## Testing

- `--asset-smoke`: material import (every field, bad JSON, missing texture/shader), edit + reimport changes the cooked data, pak round trip,
  model extraction (files created, prefab references them, re-import keeps user edits, removed material keeps file), shader import (valid
  struct, every parameter type, 256-byte limit, bad annotation, compile error keeps the previous cooked version).
- `--scene-smoke`: a MeshRenderer with `.wmat` materials round-trips; prefab instances point at extracted materials.
- `--render-test` on Vulkan/D3D11/D3D12 with `--gpu-validation` and cross-backend images: a custom shader changes the pixels; a parameter edit
  changes them; `VertexOffset` moves geometry in the color, depth and shadow passes consistently; a masked custom material cuts out in prepass
  and forward alike; TAA velocity of a custom-shader mesh is correct (no ghost trail); a failed shader draws magenta and the rest of the frame
  is intact; hot reload swaps the pipeline. Every task has a negative control.

## Later steps (own specs)

2. Full pixel-shader replacement mode: opt-in `Unlit`/custom stage where the shader supplies the final color; engine still supplies the
   depth, shadow, velocity and pick variants where it can.
3. User post effects: a per-scene stack of full-screen effect shaders reusing the annotated-parameter parser and panel code; a "Post effects" tab.

## Out of scope here

Preview rendering of materials, material instances/overrides, node-based shader graphs, shaders for UI/particles/sky, compute materials.

## Risks and assumptions

- Compiling variants by including user code into engine shaders makes engine include changes ripple into user shaders; the contract file
  (`surface_contract.hlsli`) is versioned and documented in `docs/`.
- Many shaders mean many pipelines; the cache is bounded by the number of shaders used by visible materials. Not optimised further here.
- The 256-byte block and four user textures are limits chosen for simplicity and can be raised later.
- Pick/outline use unmodified geometry (documented).
