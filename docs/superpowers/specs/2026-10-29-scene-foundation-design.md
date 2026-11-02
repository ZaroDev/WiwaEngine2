# Scene Foundation (sub-project 2b-i) Design

Part of the renderer integration (`PROMPT.md`). The renderer is split into 2a Foundation (done), 2b Scene path and 2c Post and settings. 2b is split again: **2b-i Scene foundation** (this spec: EnTT scene, components, reflection, the `.wscene` asset, linked prefab instances, async loading, editor panels) and **2b-ii Render path** (extraction into render commands, GPU cache, geometry, lighting, shadows). Nothing here draws anything.

## Goal and success criteria

A scene can be authored in the editor, saved as a `.wscene` asset, cooked into `Library`, and loaded asynchronously by a game project (no editor code) through `--scene <path>`, producing an EnTT registry the renderer can read in 2b-ii.

- `--scene-smoke` exits 0 in Debug and Release; each check is shown to fail under a negative control.
- Loading never stalls a frame: commit work is time-sliced under a per-frame budget.
- Debug, Release and Shipping build; the game project links no editor code.

## Constraints carried over

- `wiwa` namespace, `WI_LOG` (no `std::cout`), `Ref`/`Scope`, tabs, `#include <pch.h>` first in the engine project.
- Premake only. EnTT is vendored under `engine/vendor/entt` (header-only) and added to the include dirs in `premake5.lua`.
- Components store `AssetRef`, never `Handle<T>`.
- Editor-only code lives in the editor project; no build-config macro decides editor vs game.
- The scene lives on the main thread. Anything built off-thread works on immutable data.
- Docs under `docs/superpowers/` are not committed.

## Components (engine project)

| Component | Fields |
|---|---|
| `Id` | 128-bit `EntityGuid`, stable across save/load |
| `Name` | string |
| `Transform` | local position (vec3), rotation (quat), scale (vec3) |
| `Hierarchy` | parent, first child, next sibling (runtime links; saved as the parent's `EntityGuid`) |
| `WorldTransform` | cached matrix, recomputed by a system for dirty subtrees; not saved |
| `Camera` | projection (perspective/orthographic), fov, near, far, ortho size, primary flag |
| `Light` | kind (directional/point/spot), color, intensity, range, spot angles, casts shadows |
| `MeshRenderer` | mesh `AssetRef`, per-primitive material override `AssetRef` list |
| `PrefabInstance` | Prefab `AssetRef`, override list (below) |

A script component arrives with Lua scripting, through the value-bag reflection path below.

`Scene` owns the `entt::registry` and an `EntityGuid`-to-entity map. `SceneModule` (an engine module) owns the active scene and the loader.

## Reflection

One description of each component feeds the serializer, the inspector and the override engine.

- `FieldDescriptor { name, FieldType, access, default, metadata }`. `FieldType` is a closed set: bool, int, float, vec2, vec3, vec4, color, quat, string, enum, `AssetRef` (with the expected asset type), entity reference.
- **Macro field tables.** `WI_COMPONENT(Type, WI_FIELD(a), WI_FIELD(b), ...)` expands to a static `ComponentInfo` holding a `FieldDescriptor` array built from `offsetof` and a `FieldType` trait on the member type. A member type with no trait is a compile error.
- **Two access backends from day one:**
  - *struct-offset:* C++ components.
  - *value-bag:* a runtime-built `ComponentInfo` over a name-to-`FieldValue` map. This is how Lua script properties will appear in the editor: a script declares `properties = { speed = {type="float", default=5, min=0} }`, the scripting module builds a `ComponentInfo` from it, and a `ScriptComponent` holds the values. A script is addressed as `Script:<script asset>.field`, so an entity may carry several scripts. When the script file changes, values whose name and type still match are kept, the rest take their defaults.
  - Lua itself is not built in 2b-i; a test registers a fake dynamic component through the value-bag path.
- `ComponentRegistry` maps name and id to `ComponentInfo` and holds the EnTT hooks (has, add, remove, raw pointer), so generic code never needs the C++ type.
- Fields are addressed by the path `Component.field`.

## The `.wscene` asset

- **Source (`Assets/`):** JSON, human-readable and diffable: a list of entities, each with `id`, `parent` and its components as `"Name": { fields }`. An `AssetRef` is written `{ "guid": ..., "localId": ... }`.
- **Importer (editor project):** validates the file and cooks a binary `SceneData` into `Library`, recording every referenced `AssetRef` as a dependency so `preload()` can load a scene's content ahead of time.
- **Compatibility:** an unknown component or field is preserved and logged once, so an older engine does not destroy newer data. A known field with the wrong type fails the import.
- **Limits:** the binary read uses the bounds-checked `BinaryArchive` with hard caps on entity and override counts.

## Linked prefab instances

- The scene saves only the instance root entity with `PrefabInstance { prefab, overrides }`. At load, children are generated from the Prefab. Their `EntityGuid` is derived from the instance's id and the node's stable id, so ids are identical on every run.
- **Stable node ids (change to the existing model importer and `PrefabData`):** each `PrefabNode` gets a stable id, a hash of its name path with a suffix when siblings share a name. `PrefabData` stores it.
- **Overrides (full):** each record is one of: set field `{node id, "Component.field", value}`, add component, remove component, add child entity under a node, remove node.
- **Re-import reconciliation:** an override whose node id no longer exists is dropped with a warning and listed in the editor. It is never applied to a different node.
- **Revert:** per field (restores the Prefab value) and per instance.
- **Unpack:** an explicit editor action that flattens an instance into plain entities.
- Nested prefab instances (a Prefab inside a Prefab) are out of scope.

## Async loading

Only the final commit touches the registry; everything before it works on immutable data.

1. **Build (job, off-thread):** read `SceneData` through the `AssetManager` and build a `SceneBlueprint`: entities with component values decoded, prefab instances expanded, overrides applied, ids derived.
2. **Preload:** the blueprint's `AssetRef`s go to `AssetManager::preload()` so content loads in parallel while the scene waits.
3. **Commit (main thread, time-sliced):** entities are created in batches under a per-frame budget (default 2 ms). The scene is marked Ready only after the last batch and is invisible to the renderer until then.

`SceneModule::load_async(AssetRef, LoadMode) -> JobProgress` covers all three stages.

- **Replace:** the old scene keeps living and rendering until the new one is Ready, then swaps in one frame; the old registry is destroyed in slices. No blank frame.
- **Additive:** the new entities merge into the active scene.
- **Cancel:** loading another scene cancels the load in progress (stage 1 checks a flag; stage 3 stops and discards).
- **Failure:** a missing or corrupt file, or a failed dependency, reports Failed with the reason and leaves the current scene untouched.
- A missing `AssetRef` target does not fail the load: the entity keeps the reference, the renderer will skip it and the inspector shows it in red.

## Editor (editor project)

- **Hierarchy panel:** entity tree, create, delete, reparent by drag, drop a Prefab asset to add an instance.
- **Inspector:** one widget per `FieldType` from the descriptors; override markers and revert on prefab children.
- **Save / Open:** Save writes the `.wscene` JSON and the file watcher re-imports it. Open uses `load_async`, so the editor does not freeze.
- Panels draw from their own `update()` with `ImGui::`, as the existing Renderer panel does.

## Error handling

- Import errors carry the JSON path (`entities[12].MeshRenderer.mesh: not an AssetRef`) and keep the previous cooked version in use.
- Duplicate entity ids, a missing parent, or a parent cycle fail the import.
- Corrupt binary data fails the load cleanly under the caps above.

## Testing

`--scene-smoke` (runs headless on a command thread in the editor, like `--asset-smoke`) checks:

- save/load round trip of every component;
- the value-bag component through serializer, inspector description and overrides;
- prefab expansion with ids that match across two runs;
- each override kind; revert; unpack;
- re-import with a node removed (override dropped and reported);
- unknown fields preserved; wrong-typed field rejected; duplicate ids, missing parent and cycle rejected; corrupt data rejected;
- a large generated scene: every commit frame stays within the budget;
- cancel mid-load leaves the active scene unchanged; replace never has a frame without a scene.

Each check has a negative control demonstrating that it can fail. `--scene <path>` is verified in a game-project run with no editor code.

## Out of scope

Rendering the scene (2b-ii), Lua scripting itself, gizmos and viewport picking, undo/redo, region streaming, nested prefab instances, and any scene format compatibility with the standalone renderer.

## Task order (for the plan)

1. Vendor EnTT; `Id`, `Name`, `Transform`, `Hierarchy`, `Scene`, world-transform system.
2. Reflection: `FieldDescriptor`, macro tables, `ComponentRegistry`, both backends.
3. `Camera`, `Light`, `MeshRenderer`, `PrefabInstance` components.
4. `.wscene` JSON read/write, `SceneData` binary, Scene importer, dependency recording.
5. Stable node ids in the model importer and `PrefabData`.
6. Prefab expansion and the override engine.
7. `SceneBlueprint`, `SceneModule::load_async`, time-sliced commit, replace/additive, cancel/failure.
8. `--scene` argument and `--scene-smoke`.
9. Editor: Hierarchy, Inspector, Save/Open.
10. Acceptance on Debug, Release and Shipping.
