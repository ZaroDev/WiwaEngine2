# Jolt physics integration, cycle 1 (core)

Date: 2026-11-02. Status: draft for review.

## Goal

Integrate Jolt Physics (https://github.com/jrouwe/joltphysics) into the engine. Jolt runs on its own physics thread. Cycle 1 delivers the foundation and the core rigid-body feature set: bodies, shapes, physics material assets, collision and trigger events and queries exposed to Lua, and an editor Physics panel for settings.

Jolt has many features. They are split into cycles, each with its own spec, plan and implementation:

| Cycle | Content |
| --- | --- |
| 1 (this spec) | Physics thread, body and shape sync, rigid bodies, primitive, convex hull, mesh and compound shapes, physics materials, layers, contact and trigger events, queries, Lua bindings, settings panel, debug draw |
| later | Constraints and joints, character controller, vehicles, ragdolls, soft bodies, heightfield shapes |

Out of scope for cycle 1: everything in the "later" row. The design must not block them (the body table and command queue take new command kinds without restructuring).

## Decisions taken in brainstorming

- Threading: **free-running fixed step**. The physics thread owns the Jolt world and steps on its own clock.
- Lua sync: `on_physics_update(self, dt)` runs on the **main thread, once per completed physics step**. Queries run against that step's read-only snapshot.
- Settings: **project layers, scene gravity**. Layer names and the collision matrix are project-wide. Gravity and step settings are per scene.

## 1. Vendoring and build

- Jolt is vendored under `engine/vendor/JoltPhysics` and built as a premake static library `Jolt`. The `wiwa` project links it. Nothing is built with CMake.
- Build flags: object stream off, cross-platform determinism off. `JPH_DEBUG_RENDERER` on in Debug and Release, off in Shipping. The same defines must be set for the Jolt lib and for every engine file that includes Jolt headers (a mismatch is an ODR bug in Jolt).
- Jolt headers are included only from `engine/src/physics/` (private). The main thread and the rest of the engine see only `physics/` interfaces and plain-data types. `JPH::` never appears in components, scripting bindings or editor code.
- `pch.h` does not include Jolt.

## 2. Module and lifetime

- `PhysicsModule` (`IModule`) is registered in `Engine` after `SceneModule` and before `ScriptingModule`.
- A physics world exists only while a scene is playing (the Play-mode clone, or the game's loaded scene running on a playing clock). The editor's edit mode has no simulation. Stop discards the world.
- The module owns the physics thread and the command and snapshot channels. It never calls `Engine::get_module` from the physics thread.
- The module is used by the game too, so nothing in it depends on editor code.

## 3. Physics thread

Modeled on `render_thread` and `render_snapshot.h`.

- The thread owns `JPH::PhysicsSystem`, the Jolt job system (workers separate from the engine `JobSystem`), the temp allocator, the body table (`EntityGuid` to `BodyID`) and the shape cache keyed by `AssetRef` GUID.
- Stepping: fixed rate (default 60 Hz, scene setting) with an accumulator. The step is `PhysicsSystem::Update(fixedDt, collisionSteps, ...)`. A cap on steps per wake (default 4) prevents a spiral of death. When the cap hits, the simulation falls behind real time instead of stalling.
- **Main to physics: command queue**, a bounded MPSC queue of plain-data commands: `CreateBody(desc)`, `DestroyBody`, `SetTransform` (teleport), `SetKinematicTarget`, `SetVelocity`, `AddForce`, `AddImpulse`, `AddTorque`, `SetMaterial`, `SetLayer`, `SetGravity`, `SetSettings`, `ReplaceShape`. Bodies are named by `EntityGuid`. Commands are applied at the start of the next step, in submission order. If the queue is full, the producer blocks briefly and logs a warning (never drops silently).
- **Physics to main: step snapshots.** After each step the thread publishes an immutable snapshot (step index, time, body poses and velocities, active flags, the step's contact and trigger events). Snapshots go through a small ring buffer. The main thread reads the latest and keeps the previous one for interpolation. Old snapshots are released when the ring wraps. The main thread never reads Jolt structures.
- **Query data.** The snapshot also carries a read-only query structure for that step (a compact copy of the body poses and shared pointers to immutable shape data, with a simple BVH for the dynamic bodies, static ones live in a persistent structure rebuilt when static bodies change). Queries never touch the live Jolt world.
- **Contact listener** (`JPH::ContactListener`) runs on the physics thread and appends begin, persist and end contacts and trigger enter and exit to a per-step event buffer that goes into the snapshot. Persist events are throttled to one per pair per step and only for bodies that opted in with `ReportContacts`.
- The body table, shape cache and listener are touched only by the physics thread, so they need no locks.
- Errors (shape creation failure, an unknown asset) are logged with `WI_PHYSICS_LOG` and the body is created as a missing-shape placeholder that does not collide. They never reach the main thread as a crash.

## 4. Main-thread flow per frame

1. `PhysicsModule::update` (priority before `ScriptingModule`): take the newest snapshots, then for each completed step in order (capped at the latest N steps if the main thread fell behind) dispatch, to Lua through `ScriptInstances`:
   1. the step's collision and trigger handlers (`on_collision_enter/stay/exit`, `on_trigger_enter/exit`),
   2. `on_physics_update(self, dt)` with `dt` equal to the fixed step.
   Steps that exceed the cap are skipped for script dispatch but their events are still delivered (begin and end events are never lost, only persist events may be coalesced).
2. Gameplay scripts and `update()` run.
3. `pre_render_update`: write interpolated poses into `Transform` for dynamic bodies by the fraction of a step that has elapsed since the latest snapshot, then Scene updates world transforms. Kinematic and static bodies are driven from the scene: when their `WorldTransform` changed since last frame the module sends `SetKinematicTarget` or `SetTransform`.
4. The render snapshot is built from the same transforms, so what the renderer shows matches what the scripts saw.

Entities destroyed on the main thread send `DestroyBody`. Events that reference an entity destroyed in between are dropped on the main thread by GUID lookup.

## 5. Components

Registered through `WI_COMPONENT` and `WI_FIELD`, so the serializer, inspector and prefab override engine work with no extra code. They store `AssetRef`, never handles.

`RigidBody`
- `Motion`: Static, Kinematic, Dynamic
- `MassOverride` (0 = from shape and density), `LinearDamping`, `AngularDamping`, `GravityFactor`
- `ContinuousCollision` (CCD, Jolt linear cast motion quality), `AllowSleeping`, `ReportContacts`
- `LockPosition` and `LockRotation` per axis (allowed DOFs)

`Collider`
- `Shape`: Box, Sphere, Capsule, Cylinder, ConvexHull, Mesh, Compound
- `Size` (shape dependent: half extents, radius, half height), `Offset`, `Rotation`
- `IsTrigger` (sensor), `Layer` (index into the project layers)
- `Material`: `AssetRef` of `AssetType::PhysicsMaterial`
- `Mesh`: `AssetRef` of `AssetType::Mesh`, for ConvexHull and Mesh shapes

Shape rules:
- A `Collider` on an entity with a `RigidBody` is that body's shape. A `Collider` with no `RigidBody` and no ancestor `RigidBody` is a static body.
- A `Collider` on a descendant of a `RigidBody` entity (no `RigidBody` of its own) is added to the ancestor's compound shape with its local transform. Compound shapes are rebuilt (and sent as `ReplaceShape`) when a child collider changes.
- The entity's world scale scales the shape. Non-uniform scale is allowed for box, convex hull and mesh and is applied through Jolt's scaled shapes. Sphere and capsule need uniform scale, otherwise the largest axis is used and a warning is logged once.

## 6. Assets

- New `AssetType::PhysicsMaterial`, source `.wphysmat` (JSON): `friction`, `restitution`, `density`, `frictionCombine` and `restitutionCombine` (Average, Min, Max, Multiply). It is imported and cooked by the editor in `editor/src/asset_pipeline/importers/physics_material_importer.cpp`, like the material importer. The engine has the runtime types and loader only.
- The engine has a built-in default material (friction 0.5, restitution 0.0, density 1000) used when a `Collider` has no material.
- Convex hulls and triangle meshes are cooked by the editor model importer as sub-assets of the model (stable `localId`s), stored as Jolt shape binary state. The game and the engine never build hulls or BVH meshes from raw vertices at runtime. Hull and mesh sub-assets are produced on demand when a model is imported with the physics option enabled.
- Cooked shapes are loaded as CPU-side assets (`AssetManager::load` is async and returns `Handle<T>`). Only the physics thread holds shape objects, built from the loaded bytes.

## 7. Settings

- **Project settings** (`physics.wsettings`, next to the build settings, editor owned and packed with the game): up to 32 named layers and the 32x32 collision matrix.
- **Scene settings** (in the `.wscene` next to the environment settings, same serializer): gravity vector, fixed rate (Hz), max steps per frame, velocity and position solver iterations, sleep threshold, debug draw defaults.
- Layers map to Jolt object layers and broad phase layers (static, moving, trigger) so the broad phase stays efficient.

## 8. Lua

User docs go in `docs/scripting.md`. Bindings in `engine/src/scripting/bindings/bind_physics.cpp`.

Events (handlers on the script table, called with the script's entity as `self`):
- `on_physics_update(self, dt)`: once per completed fixed step, main thread.
- `on_collision_enter(self, other, contact)`, `on_collision_stay`, `on_collision_exit`; `contact` has `point`, `normal`, `impulse`, `relative_velocity`.
- `on_trigger_enter(self, other)`, `on_trigger_exit(self, other)`.

Queries (synchronous, against the current step snapshot, on the main thread, never blocking on the physics thread):
- `physics.raycast(origin, dir, max_dist, layer_mask)` returning a hit table or nil, `physics.raycast_all(...)`
- `physics.overlap_sphere`, `physics.overlap_box`, `physics.shape_cast`

They are guaranteed consistent inside `on_physics_update` and the collision handlers. Elsewhere (for example `update()`) they see the latest published step.

Body control (queued as commands, applied at the start of the next step; documented as taking effect one step later):
- `body:add_force`, `add_impulse`, `add_torque`, `set_velocity`, `get_velocity`, `set_angular_velocity`, `set_kinematic_target`, `wake`
- `physics.set_gravity(vec3)`

`get_velocity` and positions read the latest snapshot. Scripts only run while the game clock is playing, so the sandbox rules are unchanged.

## 9. Editor

- `editor/src/panels/physics_panel.*`: a Physics panel with tabs.
  - **Settings**: the scene settings (gravity, rate, iterations, sleep threshold). Edits go through the same undo path as other scene settings.
  - **Layers**: layer names and the editable collision matrix (the project settings), saved to `physics.wsettings`.
  - **Stats** (play mode): body count, active bodies, step time, steps per frame and dropped steps, from the physics thread.
- Inspector support for `RigidBody` and `Collider` comes from the reflection layer. Collider shape gizmos in the viewport use the existing debug renderer and gizmo code.
- Physics debug draw: a toggle draws shapes, contacts and sleeping state. The physics thread copies the draw data into the step snapshot (Jolt `DebugRenderer` implementation that records lines and triangles, no GPU access) and the main thread submits it to the existing debug renderer. Not compiled in Shipping.

## 10. Verification

There is no test framework. A new editor flag `--physics-smoke` (exit code 0 = pass) checks:

1. A dynamic box dropped on a static plane comes to rest at the expected height.
2. A stack of boxes goes to sleep.
3. A trigger fires enter and exit events exactly once each.
4. Layer filtering: two layers with the matrix cleared pass through each other.
5. Material restitution changes the bounce height (two materials, measurable difference).
6. A Lua handler receives `on_collision_enter` with a plausible normal.
7. `on_physics_update` runs at the fixed rate independent of frame time (step count over a fixed simulated duration, with a deliberately slow frame).
8. A raycast inside `on_physics_update` hits the pose of that step.
9. A force applied in `on_physics_update` takes effect on the next step and not before.
10. A cooked mesh and a convex hull sub-asset round trip through the asset pack and load.
11. The simulation is repeatable: two runs with the same commands produce the same poses within a tolerance.

Each check must be seen to fail under a negative control before it is trusted:

- the contact listener disabled makes 3 and 6 fail,
- a snapshot one step stale makes 8 fail,
- dropping the command queue makes 9 fail,
- clearing the material makes 5 fail.

`--render-test` is unaffected. The existing smoke flags must still pass.

## Risks

- **Threading bugs.** Mitigated by the one-way data flow: Jolt state is owned by one thread, and everything crossing the boundary is immutable plain data.
- **Interpolation versus gameplay.** Scripts see step poses, the renderer sees interpolated poses. The difference is bounded by one step and is documented.
- **Query snapshot cost.** Copying poses and building the query structure each step costs time with many bodies. The first version rebuilds only for active bodies and keeps statics persistent. Measure in the stats tab before optimizing.
- **Play-mode clone.** The world is built from the clone, so Stop must destroy the thread's world cleanly, and a restart must not reuse stale body ids.
