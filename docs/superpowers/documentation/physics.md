# Physics (Jolt)

Code: `engine/src/physics`, Lua `scripting/bindings/bind_physics.cpp`, editor `editor/src/panels/physics_panel.*`. Vendored under `engine/vendor/JoltPhysics`, built by premake as static lib `Jolt`. Verified by `--physics-smoke` and `--physics-panel-smoke`. Lua API: `docs/scripting.md`.

Implemented: rigid bodies, primitive/convex hull/mesh/compound shapes, materials, layers, contact and trigger events, queries, body control, joints, a character controller, debug draw. **Not built:** vehicles, ragdolls, soft bodies, heightfields. Cycle 2 design (joints, character controller, ragdolls): [jolt-physics-cycle2-design.md](jolt-physics-cycle2-design.md). The body table and command queue take new command kinds without restructuring.

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

## Joints

`Joint` (reflected; one component with a `Type`: Fixed, Point, Hinge, Slider, Distance, Cone, SwingTwist, SixDof) sits on the entity of body A and names body B in `Connected` (not set: the world, and `AnchorB` is then a world point). Anchors and axes are in each body's frame (the entity's world position and rotation, no scale; from the origin, not the centre of mass). Fields the type does not use are ignored; angles are degrees.

- **Rest pose:** the Jolt constraint is made in world space from the two bodies' poses at that moment, so that pose is zero: hinge and slider limits count from it (`LimitMin <= 0 <= LimitMax`, hinge within 180; both 0 is no limit), and a positive motor target turns or moves A relative to B, right-handed around/along `Axis` (A is Jolt's second body). Distance: `LimitMin`/`LimitMax` are the length range (both 0: the length at the start); `SpringFrequency > 0` makes it soft. Motors exist on hinge and slider (`MotorMode` Velocity or Position, `MotorMaxForce` 0 = no limit).
- **Physics thread:** `JointSet` (`joint_set.*`, with `joint_builder.*` for everything Jolt) is the joint table next to the body table. A joint is made while both bodies exist and waits otherwise (before B appears, after a body was destroyed or made again: a body made under the same key takes its joints back). A description that cannot be built (zero axis, NaN, min > max, connected to itself) is logged once and stays unmade. Unmaking a joint, and changing its motor or limits, wakes the bodies it holds (Jolt does not).
- **Commands:** `CreateJoint`, `DestroyJoint`, `SetJointMotor`, `SetJointLimits`, keyed by the guid of body A. `PhysicsSync::push_joints` sends a joint for each `Joint` on an entity that has a body: a change of only the motor or limits goes as `SetJointMotor`/`SetJointLimits` (the rest pose is kept), any other change makes the joint again; a removed component or entity sends `DestroyJoint`.
- **Pairs that do not collide:** `CollideConnected` off (the default) puts the two bodies in a set that `ContactCollector::OnContactValidate` rejects.
- **Break:** `BreakForce`/`BreakTorque` (0: unbreakable). After each step the total impulse a joint pushed with, divided by the step, is compared to them; over the limit the joint is unmade for good (a new `CreateJoint` makes another) and a `PhysicsEventKind::JointBreak` event (A the owner, B the other body or a null guid for the world) goes in the step's events, never lost. `events_for_step` needs only the owner alive.
- **Lua:** `on_joint_break(self, other)` to the owner (`other` nil for the world or a gone entity); `joint.set_motor` and `joint.set_limits` write the `Joint` component, which the sync sends one step later.
- **Debug draw** shows constraints and their limits; the Physics panel's Stats tab counts the joints made (`PhysicsStepSnapshot::JointCount`).
- Not built: ropes, gears, pulleys, rack and pinion, path constraints. `Connected` must be the entity of a body (a collider on a descendant joins its ancestor's body, which is the key to name).

## Character controller

`CharacterController` (reflected; `Radius`, `Height` (caps included), `Offset`, `StepHeight`, `MaxSlopeAngle` (degrees), `Mass`, `PushStrength`, `GravityFactor`, `Layer`) on an entity with no `RigidBody` or `Collider` of its own (one warning, no character). The entity origin is the character's feet when `Offset.y` is half the height; up is world +Y. A `Height` below `2 * Radius` is made one with a warning.

- **Physics thread:** `CharacterSet` (`character_set.*`) owns one `JPH::CharacterVirtual` per character, next to the body table and the joint table. Before each step it gives the character the velocity it was told to keep (horizontal only; `SetCharacterVelocity`), plus gravity, plus (standing on ground that is not falling away) the ground's velocity and a pending jump, then `ExtendedUpdate` (collide and slide, `mWalkStairsStepUp`/`mStickToFloorStepDown` from `StepHeight`, `MaxSlopeAngle`). A moving platform carries what stands on it. The supporting volume is the lower cap, measured from the character position. The reported velocity is the displacement over the step.
- **Inner body:** every character has a kinematic inner rigid body (in the Moving layer of its `Layer`), registered with `ContactCollector` under the character's entity guid. That is what makes a character show in queries (it is in the `QuerySnapshot` as a capsule), fire `TriggerEnter/Exit` and be hit by fast bodies.
- **Commands:** `CreateCharacter`, `DestroyCharacter`, `SetCharacterVelocity`, `CharacterJump` (applied in the next step, only if the character stands on ground then), `TeleportCharacter`. `PhysicsSync::push_characters` sends a character for each `CharacterController`: a changed field makes it again where its entity is; an entity moved by something other than the physics (the position differs from what `apply_poses` last wrote) is a teleport.
- **Snapshot:** a `CharacterPose` per character (position, velocity, `GroundState` Grounded/Sliding/Airborne, the ground entity, the ground normal) and `CharacterCount`; `apply_poses` writes the (interpolated) position to the entity's `Transform`.
- **Lua:** `character.move(entity, velocity)`, `character.jump(entity, speed)`, `character.is_grounded(entity)`, `character.state(entity)`; one step later, errors for an entity without the component.
- **Debug draw:** a capsule in a colour for the state (green grounded, yellow sliding, red airborne); the Stats tab counts the characters.
- Not built: character-vs-body `on_collision_*` events, custom up axes, crouching and shape switching, ladders. Characters push bodies through Jolt's defaults (`Mass`, `PushStrength`).

## Assets and settings

- `PhysicsMaterial` (`.wphysmat` JSON: friction, restitution, density, friction/restitution combine Average/Min/Max/Multiply; out-of-range values rejected at import). A built-in default (friction 0.5, restitution 0, density 1000) is used when a collider has none.
- Convex hulls and triangle meshes are cooked by the model importer when `generatePhysicsShapes` is set in the `.meta`: `PhysicsShape` sub-assets at the mesh's local id plus `PHYSICS_HULL_LOCAL_ID_OFFSET` / `PHYSICS_MESH_LOCAL_ID_OFFSET`, stored as Jolt shape binary state. The game and engine never build hulls/BVH meshes from raw vertices at run time. Only the physics thread holds shape objects.
- **Project settings** `physics.wsettings` (edited on the Settings page, Physics; the Physics panel keeps the scene settings and stats) (editor working directory, packed under `PHYSICS_SETTINGS_GUID`): up to 32 named layers and the 32x32 collision matrix, mapped to Jolt object layers and broad-phase layers (static, moving, trigger). **Scene settings** (in `.wscene`): gravity, fixed rate, max steps per frame, solver iterations, sleep threshold, debug-draw defaults.

## Lua

`on_physics_update(self, dt)`, `on_joint_break(self, other)`, `on_collision_enter/stay/exit(self, other, contact)` (`point`, `normal`, `impulse`, `relative_velocity`), `on_trigger_enter/exit(self, other)`; queries `physics.raycast`, `raycast_all`, `overlap_sphere`, `overlap_box`, `shape_cast`; joints `joint.set_motor`, `joint.set_limits`; characters `character.move`, `character.jump`, `character.is_grounded`, `character.state`; body control `add_force`, `add_impulse`, `add_torque`, `set_velocity`, `get_velocity`, `set_angular_velocity`, `set_kinematic_target`, `wake`, `physics.set_gravity`.

## Editor

Physics panel tabs: **Settings** (scene settings through the normal undo path), **Layers** (names and matrix, saved to `physics.wsettings`), **Stats** (play mode: bodies, active, joints, characters, step time, steps per frame, dropped steps). Debug draw: a Jolt `DebugRenderer` implementation on the physics thread records lines/triangles into the step snapshot (no GPU access) and the main thread submits them to `DebugDraw`; only compiled where `JPH_DEBUG_RENDERER` is defined (Debug, Release).
