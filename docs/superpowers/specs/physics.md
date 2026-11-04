# Physics (Jolt)

Code: `engine/src/physics`, Lua `scripting/bindings/bind_physics.cpp`, editor `editor/src/panels/physics_panel.*`. Vendored under `engine/vendor/JoltPhysics`, built by premake as static lib `Jolt`. Verified by `--physics-smoke` and `--physics-panel-smoke`. Lua API: `docs/scripting.md`.

Implemented: rigid bodies, primitive/convex hull/mesh/compound shapes, materials, layers, contact and trigger events, queries, body control, debug draw. **Not built:** constraints/joints, character controller, vehicles, ragdolls, soft bodies, heightfields. The body table and command queue take new command kinds without restructuring.

## Build rules

- `JoltDefines()` in the root `premake5.lua` sets the Jolt defines (`JPH_DEBUG_RENDERER`, profiling; asserts in Debug; none in Shipping). They must be identical for the Jolt lib and every engine file that includes Jolt headers (a mismatch is an ODR bug).
- `JPH::` types stay inside `engine/src/physics`. Components, scripting bindings and editor code see plain-data types only. `pch.h` does not include Jolt. Object stream and cross-platform determinism are off.

## Threading

`PhysicsModule` (priority -20: after the scene, before UI and scripts) owns a physics thread that owns the whole Jolt world: `PhysicsSystem`, Jolt's own job system, the body table (`EntityGuid` to `BodyID`) and the shape cache keyed by asset guid. No locks are needed on those. The thread never calls `Engine::get_module`.

- **Stepping:** fixed rate (default 60 Hz, scene setting) with an accumulator; a cap on steps per wake (default 4) stops a spiral of death - the simulation falls behind real time instead of stalling.
- **Main to physics:** a bounded MPSC queue of plain-data commands (`physics_commands.h`): create/destroy body, set transform (teleport), kinematic target, velocity, force/impulse/torque, material, layer, gravity, settings, replace shape. Bodies are named by entity guid. Applied at the start of the next step in submission order (commands drain while paused). A full queue blocks briefly and logs; it never drops silently.
- **Physics to main:** after each step an immutable `PhysicsStepSnapshot` (step index, time, poses and velocities, active flags, the step's contact/trigger events from `ContactCollector`, debug-draw data, a `QuerySnapshot`) goes through a small ring. The main thread reads the latest and keeps the previous for interpolation; it never reads Jolt structures. Step times are recorded into `PerfStats` (`PhysicsModule::pump_locked`).
- **Queries** run against that step's `QuerySnapshot` (poses, shared immutable shape data, BVH for dynamic bodies; statics persistent and rebuilt when they change), never the live world. Consistent inside `on_physics_update` and collision handlers; elsewhere they see the latest published step.
- **Contacts:** begin/end and trigger enter/exit are always delivered (never lost); persist events are throttled to one per pair per step and only for bodies with `ReportContacts`.
- Errors (shape creation, unknown asset) are logged with `WI_PHYSICS_LOG`; the body becomes a non-colliding placeholder.

## Main-thread flow per frame

1. `PhysicsModule::update` takes new snapshots. `ScriptingModule` then delivers each completed step in order (`dispatch_physics_steps`): collision/trigger handlers, then `on_physics_update(self, dt)` with `dt` = the fixed step. If the main thread fell behind, only the latest N steps run script dispatch, but their events are still delivered.
2. Gameplay scripts and `update()` run.
3. `pre_render_update`: dynamic bodies' `Transform` is written interpolated by the fraction of a step elapsed since the latest snapshot; kinematic/static bodies are driven from the scene (`SetKinematicTarget` / `SetTransform` when their `WorldTransform` changed). The render snapshot uses the same transforms, so what is shown matches what scripts saw (scripts see step poses, the renderer interpolated ones: at most one step apart).

Entities destroyed on main send `DestroyBody`; events naming an entity destroyed in between are dropped by guid lookup. Body control from Lua is queued and takes effect one step later.

## World lifetime and sync

The world exists only while a scene plays (the Play clone, or the game's scene on a playing clock); the editor's edit mode has no simulation and Stop destroys the world (a restart must not reuse stale body ids). `PhysicsSync` keeps bodies in step with the scene's `Collider`/`RigidBody` components.

## Components and shapes

`RigidBody` (motion Static/Kinematic/Dynamic, mass override, damping, gravity factor, continuous collision, allow sleeping, report contacts, per-axis position/rotation locks) and `Collider` (shape Box/Sphere/Capsule/Cylinder/ConvexHull/Mesh/Compound, size, offset, rotation, `IsTrigger`, layer index, material `AssetRef`, mesh `AssetRef`) are reflected, so serializer/inspector/overrides work for free.

- A `Collider` on a `RigidBody` entity is that body's shape; with no `RigidBody` on it or an ancestor it is a static body; on a descendant of a `RigidBody` entity it joins the ancestor's compound (rebuilt and sent as `ReplaceShape` when a child collider changes).
- World scale scales the shape. Non-uniform scale is fine for box/hull/mesh (Jolt scaled shapes); sphere/capsule need uniform scale, otherwise the largest axis is used with a one-time warning.

## Assets and settings

- `PhysicsMaterial` (`.wphysmat` JSON: friction, restitution, density, friction/restitution combine Average/Min/Max/Multiply; out-of-range values rejected at import). A built-in default (friction 0.5, restitution 0, density 1000) is used when a collider has none.
- Convex hulls and triangle meshes are cooked by the model importer when `generatePhysicsShapes` is set in the `.meta`: `PhysicsShape` sub-assets at the mesh's local id plus `PHYSICS_HULL_LOCAL_ID_OFFSET` / `PHYSICS_MESH_LOCAL_ID_OFFSET`, stored as Jolt shape binary state. The game and engine never build hulls/BVH meshes from raw vertices at run time. Only the physics thread holds shape objects.
- **Project settings** `physics.wsettings` (editor working directory, packed under `PHYSICS_SETTINGS_GUID`): up to 32 named layers and the 32x32 collision matrix, mapped to Jolt object layers and broad-phase layers (static, moving, trigger). **Scene settings** (in `.wscene`): gravity, fixed rate, max steps per frame, solver iterations, sleep threshold, debug-draw defaults.

## Lua

`on_physics_update(self, dt)`, `on_collision_enter/stay/exit(self, other, contact)` (`point`, `normal`, `impulse`, `relative_velocity`), `on_trigger_enter/exit(self, other)`; queries `physics.raycast`, `raycast_all`, `overlap_sphere`, `overlap_box`, `shape_cast`; body control `add_force`, `add_impulse`, `add_torque`, `set_velocity`, `get_velocity`, `set_angular_velocity`, `set_kinematic_target`, `wake`, `physics.set_gravity`.

## Editor

Physics panel tabs: **Settings** (scene settings through the normal undo path), **Layers** (names and matrix, saved to `physics.wsettings`), **Stats** (play mode: bodies, active, step time, steps per frame, dropped steps). Debug draw: a Jolt `DebugRenderer` implementation on the physics thread records lines/triangles into the step snapshot (no GPU access) and the main thread submits them to `DebugDraw`; only compiled where `JPH_DEBUG_RENDERER` is defined (Debug, Release).
