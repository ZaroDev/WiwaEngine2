# Jolt physics, cycle 2: joints, character controller, ragdolls

> Historical design record, written before the area was built. Its status line may be out of date. What exists now is described in physics.md; where the two differ, that doc is right.

Builds on cycle 1 (`physics.md`). Three sub-projects, built in this order, each with its own plan: **2a joints**, **2b character controller**, **2d ragdolls** (2d needs 2a). Heightfields, vehicles and soft bodies (2c, 2e, 2f) are not in this cycle. When the work lands, `physics.md`, `scripting.md` (user API in `docs/scripting.md`) and `editor.md` are updated in the same change.

## Constraints carried over

- `JPH::` stays inside `engine/src/physics`. Components, bindings and editor code see plain data. The physics thread never calls `Engine::get_module`.
- Fields are declared once (`WI_COMPONENT`/`WI_FIELD`) and feed serializer, inspector, overrides, clone, Lua and stubs. Entity-reference fields already exist in reflection.
- New commands are new alternatives of `PhysicsCommand`; they are plain data, applied in submission order at the start of the next step. Body control from Lua takes effect one step later.
- Physics and scripts run only while the game clock plays. Stop discards the world.
- Every check is a `--physics-smoke` check with a negative control that was seen to fail. Features are shown in `demo.wscene` and covered by `--demo-smoke`.

## 2a Joints

**Component** `Joint` (one component, a `Type` enum, like `Collider`), on the entity of body A:

| Group | Fields |
| --- | --- |
| Link | `Type` (Fixed, Point, Hinge, Slider, Distance, Cone, SwingTwist, SixDof), `Connected` (entity reference to body B; empty = the world), `CollideConnected` (default false) |
| Frame | local anchor on A, local anchor on B (or world point when attached to the world), primary axis, normal axis |
| Limits | min/max (angle or distance by type), swing and twist half angles |
| Spring | frequency, damping (Distance and limit springs) |
| Motor | mode (Off, Velocity, Position), target, max force or torque |
| Break | break force, break torque (0 = unbreakable) |

Only the fields that matter for `Type` are used; the inspector shows the relevant ones. Out-of-range values are clamped with one warning.

**Commands:** `CreateJoint`, `DestroyJoint`, `SetJointMotor`, `SetJointLimits`, keyed by the guid of body A.

**Physics thread:**

- A joint table sits next to the body table. A joint whose body A or B does not exist yet stays pending and is made when both do. Destroying a body destroys its joints. A body replaced by `CreateBody` on an existing key gets its joints made again.
- `CollideConnected == false`: the thread keeps the set of body pairs joined by a no-collide joint and rejects those pairs when a contact is validated.
- A joint whose force or torque goes over its break limit is removed on the physics thread; a `JointBroken` event (joint owner guid, other guid) goes into the next step's snapshot. Like trigger events, it is never lost.

**Sync:** `PhysicsSync` tracks `Joint` components like bodies: made on add, made again on change, destroyed on remove or when the entity is destroyed. A joint to an entity that has no body stays pending, not an error. A joint to the entity itself is rejected with one warning.

**Lua:** `on_joint_break(self, other)`; `joint.set_motor(entity, mode, target)`, `joint.set_limits(entity, min, max)`. Effective one step later; reads use the last step.

**Debug draw:** anchors, axes, limit arcs, in the step snapshot like the colliders.

**Not in 2a:** ropes, gears, pulleys, rack and pinion, path constraints.

## 2b Character controller

**Component** `CharacterController`, on its own entity (no `Collider` or `RigidBody` on the same entity; it has a built-in capsule): `Radius`, `Height`, `Offset`, `StepHeight`, `MaxSlopeAngle`, `Mass`, `PushStrength`, `GravityFactor`, `Layer`. Up is world +Y.

**Physics thread:** a character table keyed by entity guid owning one `JPH::CharacterVirtual` each. Per step: desired horizontal velocity plus gravity, then `ExtendedUpdate` (collide and slide, stairs, stick to floor, slope limit). Standing on a moving body inherits that body's velocity. The inner rigid body is enabled, so a character is hit by raycasts and fires the existing `on_trigger_enter/exit`, and its capsule is in the `QuerySnapshot`. Character layers use the project layer matrix.

**Commands:** `CreateCharacter`, `DestroyCharacter`, `SetCharacterVelocity` (desired horizontal velocity; the thread owns the vertical velocity), `CharacterJump` (upward speed, only when grounded), `TeleportCharacter`.

**Snapshot:** pose, velocity, ground state (Grounded, Sliding when the slope is too steep, Airborne), ground entity guid, ground normal. The main thread writes the pose to the `Transform`, interpolated like dynamic bodies. Entities destroyed in between are dropped by guid lookup.

**Lua:** `character.move(entity, velocity)`, `character.jump(entity, speed)`, `character.is_grounded(entity)`, `character.state(entity)` (ground entity, normal, velocity). One step later; reads use the last step.

**Debug draw:** the capsule, coloured by state.

**Not in 2b:** character-vs-body `on_collision_*` events, custom up axes, crouch and shape switching, ladders.

## 2d Ragdolls

A ragdoll is scene content plus two physics additions; there is no `Ragdoll` component and no skinning (the engine has none).

**Structure:** each bone is an entity with `Collider`, `RigidBody` and a `Joint` (SwingTwist or Hinge) to its parent bone. A bone with its own `RigidBody` is already a separate body, so this needs no change to body building.

**Additions:**

1. `Joint.CollideConnected` (from 2a) keeps adjacent bones from colliding.
2. A `SetMotionType` command. A change that touches only `RigidBody.Motion` switches the body in place (Jolt `SetMotionType`), keeping velocity and joints, instead of making the body again. This takes a ragdoll from Kinematic (posed and moved by its entity transforms) to Dynamic.

**Hierarchy fix:** `PhysicsSync::apply_poses` writes a dynamic body's world pose as a local `Transform` through its parent but does not update `WorldTransform`. A bone's parent is itself a dynamic body, so a child would be converted against a stale parent matrix. It must go parent before child and use the parent's just-written pose.

**Lua:** `ragdoll.activate(root, velocity?)` switches every body under `root` to Dynamic, applying the optional velocity to all; `ragdoll.deactivate(root)` switches them back to Kinematic. Both are helpers over `SetMotionType`.

**Editor helper:** "Create Ragdoll from Skeleton", an action on a `Skeleton` asset, writes the hierarchy into the scene as one undoable step: capsule colliders along the bones, SwingTwist limits from defaults, mass split by bone length, `CollideConnected` off, bodies Kinematic. It lives in `editor/src` and only writes scene data.

**Not in 2d:** skinning and animation, powered or animation-matched ragdolls, partial blends.

## Demo (`editor/assets/scenes/demo.wscene`)

Covered by `--demo-smoke`:

- **Joints demo:** a hinged door, a chain or bridge of jointed bodies, a slider with a motor, a spring (Distance) pendulum, and a breakable joint with `on_joint_break`.
- **Character controller demo:** a controllable character (Input module) on a floor with steps, a slope past `MaxSlopeAngle`, a moving platform and a trigger volume.
- **Ragdoll demo:** a humanoid ragdoll prefab, activated by a key or trigger with the character's velocity.

## Verification

All in `--physics-smoke`, each with a negative control.

- **Joints:** anchors stay together within tolerance under load; limits hold; a motor reaches its target; a joint breaks over its break force and fires the event once; a body destroyed in the middle takes its joints with it with no crash and no event for a dead guid; a pending joint is made when its second body appears; no-collide pairs do not collide; Stop and quick replay leave no stale joints.
- **Character:** stands still on a floor; walks; climbs a step of `StepHeight` and is stopped by a taller one; slides past `MaxSlopeAngle`; jumps only when grounded; is carried by a moving platform; fires a trigger; is hit by a raycast; two runs give the same result.
- **Ragdoll:** joints stay together while it falls and tumbles; limits hold; adjacent bones do not collide; `activate` and `deactivate` keep velocity; the child-pose check fails without the parent-before-child order; two runs give the same result.
- Panel: Physics panel Stats gets joint and character counts (`--physics-panel-smoke`).
