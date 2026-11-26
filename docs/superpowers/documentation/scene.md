# Scene

Code: `engine/src/scene`, editor side `editor/src/scene` and `editor/src/play`. Verified by `--scene-smoke`, `--play-smoke`, `--demo-smoke`.

The scene and ECS live on the main thread. Off-thread work uses immutable data.

## Scene and components

`Scene` owns the `entt::registry` and an `EntityGuid`-to-entity map. `SceneModule` (priority -80) owns the active scene and the loader. Registered components (`scene_components.cpp`): `Id` (128-bit `EntityGuid`, stable across save/load), `Name`, `Transform` (local TRS), `Camera`, `Light`, `MeshRenderer` (mesh + per-primitive material `AssetRef` list, `ReceivesDecals`), `Decal` ([decals.md](decals.md)), `ParticleSystem` ([vfx.md](vfx.md)), `PrefabInstance`, `Animator`, `AnimationSocket`, `SkinBinding` (animation.md), `UICanvas`, `ViewHost` (views.md), `RigidBody`, `Collider`, `Joint`, `CharacterController` (physics.md), `AudioListener`, `AudioEmitter` (audio.md), the navigation and AI components below, plus script value-bag components. Runtime-only: `Hierarchy` (parent, first child, next sibling; saved as parent guid), `WorldTransform` (cached, recomputed for dirty subtrees, never saved), `PrefabChild`, `UnknownComponents`.

## Reflection

One description of a component feeds the serializer, inspector, prefab override engine, scene clone and LuaLS stub generator.

- `WI_COMPONENT(Type, WI_FIELD(a), ...)` builds a static `ComponentInfo` of `FieldDescriptor`s from `offsetof` and a `FieldType` trait; a member type without a trait does not compile. `FieldType` is closed: bool, int, float, vec2/3/4, color, quat, string, enum, `AssetRef` (with expected asset type), entity reference.
- Two access backends: **struct-offset** (C++ components) and **value-bag** (a runtime `ComponentInfo` over a name-to-`FieldValue` map; this is how Lua script properties look to the editor).
- `ComponentRegistry` maps name/id to `ComponentInfo` plus EnTT hooks (has, add, remove, raw pointer), so generic code never needs the C++ type. Fields are addressed `Component.field`.

## `.wscene`

- Source: JSON in `Assets/` (entities with `id`, `parent`, components as `"Name": { fields }`, a `settings` object; `AssetRef` as `{guid, localId}`). The editor importer validates and cooks a binary `SceneData`, recording every referenced `AssetRef` as a dependency.
- Unknown components/fields are preserved and logged once so an older engine does not destroy newer data; a known field with the wrong type fails the import. Duplicate ids, a missing parent or a cycle fail the import; errors carry the JSON path.
- The binary read is bounds-checked with hard caps (entities, overrides, components per entity). New mandatory blocks bump the payload version; smoke requires every truncation to be rejected and old files to load defaults.

## Scene settings

`SceneSettings` (`scene_settings.h`) is the per-scene look and is saved in the scene: environment (source None/File/Atmosphere, asset, rotation, brightness, ambient intensity, hemisphere colors, atmosphere parameters), background, exposure (manual EV + auto block), bloom, tonemap and grading, AO, TAA feedback, motion blur, physics settings (gravity, rate, steps, iterations, sleep) and the scene-level script list. `sanitized()` clamps; `settings_revision` lets the extractor send only changes. Project-wide quality is separate (`RenderQuality`, rendering.md). The sun is the scene's first directional light; there is no separate sun.

## Linked prefab instances

- A scene saves only the instance root with `PrefabInstance { prefab, overrides }`. Children are regenerated at load; their `EntityGuid` derives from the instance id and the node's stable id, so ids are identical on every run.
- Overrides: set field `{node id, "Component.field", value}`, add/remove component, add child under a node, remove node. An override whose node no longer exists after re-import is dropped with a warning and listed in the editor; it is never applied to another node.
- Revert per field and per instance; Unpack flattens an instance into plain entities.
- **Nested prefabs.** A `PrefabNode` may hold an `Instance { Prefab, Overrides }` of another prefab (a `.wprefab` or a model prefab), up to `MAX_PREFAB_DEPTH` (8) levels. `expand_prefab(root, prefab, resolver)` follows it into the prefab the resolver returns: the inner nodes become children of that node with ids `compose_node_id(outer id, inner id)` (never 0, never `ADDED_CHILD_NODE`), the overrides stored on the instance are applied first (rewritten onto the composite ids) and the root's overrides after, so a scene can override a node at any depth. Entities get `PrefabChild { Instance, NodeId, NestedRoot }` (`NestedRoot` marks the root of an inner instance; the hierarchy colors it differently). A nested prefab that is missing, still loading, would make a cycle (A to B to A) or is deeper than the limit leaves the node a plain entity with one warning; the load never fails. An override on a deep node that the inner prefab lost is dropped with a warning. Two instances of one inner prefab inside an outer one get distinct guids and independent overrides. The expander stays records-only and worker-thread safe (the resolver reads immutable `PrefabData`); the two-argument `expand_prefab` has no resolver, so nested instances count as unavailable. Cooked prefabs written before nesting have no instances and still load. In the editor, making a `.wprefab` from a hierarchy subtree that contains a prefab instance (the drag into the Asset Browser) stores that instance as a nested node (`prefab_from_records`: its prefab and its overrides; its generated children are not written); `SceneEditor` expands with a resolver and waits (`nested_state`) while an inner prefab loads.
- Editor: dragging a hierarchy entity into the Asset Browser makes a `.wprefab` and swaps the entity for an instance whose root takes the entity's name/parent/transform (the entity becomes a child node with identity transform).

- **Instances made at run time** (`PrefabSpawner`, `scene/prefab_spawner.*`, owned by `SceneModule::prefabs()`, Lua `instantiate_prefab`): `spawn` creates the root with its `PrefabInstance` at once and queues it; `SceneModule::update` (before the scripts) calls `update(active scene)`, which, as soon as the prefab and what it nests are Ready (`nested_prefabs_state`), runs the same `expand_prefab` as the editor's `expand_instance` over the root's current record (so a Transform the script set is kept) and makes the entities with a `PrefabChild` each. A root that no longer exists is dropped; a prefab that fails to load logs one error and leaves the root empty. `destroy` of the root takes the children (hierarchy). Handles to prefabs are kept in the spawner so they stay loaded. Verified in `--script-smoke` (`instantiate_prefab: ...`, on a glTF the check writes and imports).

## Async loading

`SceneModule::load_async(AssetRef, LoadMode) -> JobProgress`. Only the final commit touches the registry.

1. **Build** (job): `SceneData` to a `SceneBlueprint` (components decoded, prefabs expanded, overrides applied, ids derived).
2. **Preload**: the blueprint's refs go to `AssetManager::preload`.
3. **Commit** (main thread): entities created in batches under a per-frame budget (default 2 ms); the scene is Ready only after the last batch and invisible to the renderer until then.

Replace keeps rendering the old scene until the new one is Ready, then swaps in one frame (the old registry is destroyed in slices). Additive merges. Loading another scene cancels the one in progress. A missing/corrupt file or failed dependency reports Failed and leaves the current scene alone. A missing `AssetRef` target does not fail a load (the inspector shows it red, the renderer skips it).

## Scene clone and play mode

`clone_scene` (`scene_clone.cpp`) deep-copies a scene: same guids, hierarchy and sibling/root order, all reflected components via `ComponentRegistry`, value-bags, `PrefabInstance` with overrides, `PrefabChild`, `UnknownComponents`, settings; world transforms recomputed. Editor Play runs on a clone and Stop discards it (see editor.md). A snapshot/restore through the scene-record layer was rejected because it does not save `PrefabChild`.

## Time

`Time` has real-time, engine and game clocks. `Time::init()` starts the game clock (a game runs by default); the editor stops it after init and starts it on Play. `Clock::start()` sets `LastTime` so the first delta is not time-since-boot. Scripts and physics run only while the game clock is playing and not paused.

## Animation components

`expand_prefab` adds an `Animator` (skeleton, the model's clips, the first clip) to the root of an instance of a model that has animations, unless the scene's record of the root already has one, and a `SkinBinding` to every node with a skeleton. Clones of a scene copy them like any component; the runtime state (`AnimatorInstance`) is not copied: the play-mode clone starts with fresh instances (animation.md).

## Navigation components

`NavMeshVolume`, `NavAreaTag` and `NavAgent` are ordinary reflected components (see [navigation.md](navigation.md)). Masks are `i32`; the baked mesh is an `AssetRef` (`AssetType::NavMesh`); the walking state of an agent lives in `NavModule`, not in the component, so a clone starts with idle agents.

## AI components

`AIController`, `Perceiver` and `Perceivable` are ordinary reflected components ([behavior-trees.md](behavior-trees.md)). `AIController::Overrides` is a JSON text field; the state of a run lives in the `AIModule`, so a clone starts every tree from the beginning.
