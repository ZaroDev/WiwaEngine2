# Vehicle physics

## Intent

The first engine feature for a car game in the spirit of Assetto Corsa: **realistic but approachable**. A car should be believable at the limit (weight transfer changes grip, braking while cornering costs grip, surfaces differ) and still be drivable on a gamepad thanks to assists. Everything else on the car-game list (chase camera, splines, driving AI, force feedback, engine audio, skid marks, terrain) builds on this.

## Goals

- A car is set up in the editor from components: `Vehicle` on the body, `VehicleWheel` on each wheel child entity, tuning in a `.wvehicle` asset.
- Jolt's `VehicleConstraint` + `WheeledVehicleController` simulate suspension, wheel casts, engine, gearbox and differentials; **our own tire model** (Pacejka-style magic formula with load sensitivity and combined slip) decides grip through Jolt's `TireMaxImpulseCallback`.
- Assists on the physics thread: traction control, ABS, stability control, speed-sensitive steering with filtering, automatic gearbox and auto-clutch. Strengths in the asset; scripts can override per car.
- Simple aero: drag and downforce from coefficients in the asset.
- Surfaces: `PhysicsMaterial` gains a tire grip factor (asphalt vs grass vs gravel).
- Lua drives the car and reads its state (speed, rpm, gear, per-wheel slip, load, surface, aligning torque) for HUD, audio and effects.
- The demo hall has a drivable car; `--demo-smoke` drives it.

## Non-goals

- Steering wheel/pedal devices and force feedback output (later, list item 8). The aligning torque is computed and published now so FFB can use it.
- Tire temperature, wear, pressure; camber and toe (Jolt has none); damage; clutch pedal simulation for manual drivers beyond auto-clutch.
- Motorcycles and tracked vehicles (Jolt has them; not exposed).
- Driving AI, chase camera, engine audio, skid marks, replays, multiplayer. Separate features.

## Anti-goals

- Jolt stays unmodified (vendoring rule). The tire model enters only through public hooks.
- No `JPH::` outside `engine/src/physics`; the main thread never reads Jolt structures (snapshots only).
- A car must not feel like a floaty arcade car (grip linear in load, no combined slip): that is what approach A would have given and why it was rejected.
- Not deterministic-breaking: same inputs and rate give the same result (`--physics-smoke` repeatability already checks bodies; vehicles join it).

## Constraints

- Physics thread owns the world; main thread talks through commands and reads immutable `PhysicsStepSnapshot`s (physics.md).
- Component fields described once via `WI_COMPONENT`/`WI_FIELD`; components store `AssetRef`, never handles; `AssetType` appended before `Last`.
- Importers/cooking only in `editor/src/asset_pipeline`.
- Physics runs only while the game clock plays; Play runs on a scene clone.
- Every feature showcased in `demo.wscene` and covered by `--demo-smoke`.

## Approaches considered

- **A. Jolt's wheeled vehicle as-is**, friction curves exposed. Fastest, but grip is linear in load and combined slip is crude: arcade feel.
- **B. Jolt's vehicle + our tire model via `TireMaxImpulseCallback` + our assists (chosen by the user).** Keeps Jolt's suspension, casting, drivetrain and solver; adds load sensitivity, combined slip, aligning torque. Moderate cost.
- **C. Fully custom vehicle** (own raycasts, drivetrain, tire forces on a rigid body). Most control, several times the work, needs ~333 Hz to be stable; B already gives what a player notices.

## Design

### Authoring

- **Body entity:** `RigidBody` (dynamic) + `Collider` (typically a chassis convex hull) + `Vehicle`.
- **`Vehicle` component** (reflected): `Settings` (`AssetRef` to `.wvehicle`), `Enabled`. Runtime inputs are not fields (they come from Lua).
- **`VehicleWheel` component** on any descendant of the body with no other `RigidBody` in between (my call, so imported car models with nested wheel nodes work) (reflected): `Radius`, `Width`, `SuspensionMinLength`, `SuspensionMaxLength`, `SuspensionFrequency` (Hz), `SuspensionDamping` (ratio), `Steers` (bool), `MaxSteerAngle` (deg), `Brakes`, `Handbrake`, `Axle` (physical axle, 0 = front, 1 = rear, ...), `Side` (Left/Right), `Inertia`. `Axle` + `Side` identify a wheel; drive, anti-roll bars and tire choice all refer to axles, never to wheel indices. Suspension values sit on the wheel so front and rear can differ without asset variants.
- **Wheel position (my call):** the wheel entity's authored position is the wheel centre at ride height, as the artist sees the model. The suspension attachment point is placed above it so that ride height sits at the middle of the travel (`(Min+Max)/2`), and `mSuspensionPreloadLength` is computed from each wheel's share of the mass so the car settles there instead of sagging. Suspension direction is the body's local -Y, steering axis local +Y, forward local -Z (Jolt `mForward`).
- **Centre of mass (my call):** `RigidBody` gains `CenterOfMassOffset` (vec3, body space, zero = the shape's own). The shape builder wraps the body's shape in an offset-centre-of-mass shape when it is non-zero. It belongs on the body, not the vehicle, because any body can use it; a car sets it low and uses `MassOverride` for a realistic mass. Without it a chassis hull rolls over and weight transfer is wrong.
- Wheel children must not have a `Collider` (would join the body's compound); one warning and the collider is ignored for the compound. Wheels with no `Vehicle` parent get one warning.
- **`.wvehicle` asset** (JSON source, cooked to `VehicleSettingsData`, new `AssetType::Vehicle` appended before `Last`, importer `vehicle_importer.cpp`, file read/write in `editor/src/asset_pipeline/vehicle_file.*`, create template, reference finder entry):
  - Engine: torque curve (rpm → Nm points), max/min rpm, inertia, angular damping.
  - Transmission: mode (auto/manual), gear ratios, reverse ratios, final drive, shift up/down rpm, shift time, clutch strength.
  - Differentials: list, each naming a driven axle (its Left and Right wheels), ratio, limited-slip ratio, engine torque share. An axle not listed is undriven (a rear-drive car lists axle 1 only).
  - Anti-roll bars: stiffness per axle.
  - Tires: front and rear `TireModel` (magic formula `B C D E` for lateral and longitudinal, nominal load `Fz0`, load sensitivity, combined-slip method fixed to ellipse, pneumatic trail for aligning torque). Axle 0 uses front, other axles rear (my call; per-wheel override is a non-goal for v1).
  - Aero: drag coefficient × frontal area, downforce coefficient (front/rear split), applied at the centre of mass and split points as forces each step.
  - Assists: TC, ABS, stability, steering help strength (0..1 each), steering speed (rate limit), speed-sensitive steering limit curve.
  - Brakes: max brake torque, handbrake torque, front/rear bias.
- **`PhysicsMaterial`:** `TireGrip` (default 1.0) appended; payload version 2; a v1 payload loads with 1.0.

### Physics thread

- `VehicleSet` (`vehicle_set.*`, `vehicle_builder.*` for Jolt, `tire_model.*` Jolt-free and unit-testable) next to bodies, joints and characters. A vehicle is built once its body exists and waits otherwise (as joints do); a description that cannot be built is logged once and stays unmade.
- Commands: `CreateVehicle` (body guid, wheel descs in body space, `VehicleSettingsData`), `DestroyVehicle`, `SetVehicleInput` (throttle 0..1, brake 0..1, steer -1..1, handbrake 0..1), `ShiftGear` (+1/-1), `SetVehicleAssists` (override strengths). `PhysicsSync::push_vehicles` sends them; a changed component, wheel, or reloaded asset makes the vehicle again on the same body (the body keeps its pose and velocity), so tuning the `.wvehicle` during Play applies live.
- **Surface grip (my call):** tire grip is `TireGrip` of the contacted shape's material times the tire model's `D`; the material's `Friction` (tuned for sliding props) is ignored for tires, so existing `.wphysmat` files don't change how cars drive and the tire coefficients mean the same on every default surface. Jolt's default vehicle combine function reads `body.GetFriction()`, which the engine never sets, so a custom `CombineFunction` reads `material_of(body, subShape)` and returns `TireGrip`. Jolt's curves are set flat (1.0).
- The `TireMaxImpulseCallback` computes per-wheel max longitudinal/lateral impulse from the tire model with `Fz = suspension impulse / dt`, signed slip ratio and slip angle (taken from the wheel's state: Jolt passes magnitudes only), the friction ellipse and the grip above. Note for tuning: Jolt's friction constraint holds the tire to zero relative velocity up to that limit, so below the peak a tire grips fully instead of producing a slip-proportional force. That is the accepted cost of B.
- **Assists (my call on mechanism):** Jolt takes driver input for the whole car only. Traction control cuts throttle for the whole car and brakes a spinning driven wheel; ABS and stability control brake single wheels. Per-wheel brake torque: before each step the vehicle set scales each wheel's own `WheelSettingsWV::mMaxBrakeTorque` (we hold the settings objects Jolt references) and gives Jolt a full brake input when any wheel brakes (our code, Jolt files untouched). Stability brakes one wheel from the yaw-rate error against a bicycle-model target; steering is rate-limited and capped by speed. Aero forces are added to the body each step.
- **Gearbox:** the asset's transmission mode is the default; a script override (`vehicle.set_assist(e, "auto_gearbox", 0|1)`) replaces it for that car. The assist list holds no second copy. **Reverse in automatic (user's call, a placeholder until the game decides):** holding brake at a standstill engages reverse and the brake input then drives backward; throttle returns to forward. Manual mode reverses through `ShiftGear`.
- `VehicleCollisionTester`: cylinder cast (my call, smoother over kerbs than a ray), layer from the body's collider layer.
- Debug draw: Jolt's vehicle `Draw` into the existing debug renderer.

### Snapshot and main thread

- `VehiclePose` per vehicle in `PhysicsStepSnapshot`: body guid, speed (m/s), engine rpm, gear, clutch, applied (post-assist) inputs, assist activity flags; per wheel: steer angle, spin angle, suspension length, in contact, contact entity, contact surface grip, slip ratio, slip angle, load (N), aligning torque. `VehicleCount` for stats.
- `apply_poses` writes each wheel child's local `Transform` (attachment point − suspension length along -Y, steer about Y, spin about the axle), interpolated between the previous and latest snapshot like body poses (spin angle interpolated by the shortest difference), converted into the wheel's parent space when the wheel is nested. The authored wheel transform is cached when the vehicle is made, so the sync's change detection compares against it and the physics-written transform never triggers a rebuild. In edit mode wheels stay where authored.

### Lua

In the style of `character.*`: `vehicle.set_input(entity, throttle, brake, steer, handbrake)`, `vehicle.shift_up/shift_down(entity)`, `vehicle.set_assist(entity, name, strength)`, `vehicle.state(entity)` (speed, rpm, gear, inputs, assists active), `vehicle.wheel(entity, index)` (the per-wheel values above). LuaLS stubs regenerated. Documented in `docs/scripting.md`.

### Editor

- Inspector shows `Vehicle`/`VehicleWheel` through reflection (no hand-written UI).
- Asset Editor: a `.wvehicle` editor with grouped fields, a plot of the engine torque curve and of tire force vs slip at three loads (so tuning the formula is visible). Undo through the Asset Editor's existing edit model.
- Physics panel Stats counts vehicles.
- Scene gizmo: wheel radius circle and suspension travel line for each `VehicleWheel` while selected.

### Demo

A car in `demo.wscene` (written by `scripts/gen_demo_scene.py`; a CC0 car model or primitive boxes/cylinders if none is available) on a small loop with an asphalt and a grass `.wphysmat`, a `drive.lua` script reading input actions from a `.winput` (triggers/stick and WASD/space/Q/E), a `.wvehicle` tuned for a road car. Demo scene physics rate 120 Hz. `--demo-smoke` feeds throttle, then steer, then brake through the script path and checks the car moved forward, turned toward the steer sign, and stopped.

### Verification

- `--physics-smoke`: tire model unit checks (peak at the formula's slip, load sensitivity lowers μ at higher load, combined slip lowers lateral force under braking); flat-ground car accelerates, reaches a plausible speed, brakes to a stop; steering yaws the right way; ABS keeps braked wheels turning, TC caps driven-wheel slip; lower `TireGrip` gives less lateral acceleration; vehicle repeatability; wheel transforms written; vehicle waits for its body and is rebuilt on asset change.
- `--asset-smoke`: `.wvehicle` import/round trip, `PhysicsMaterial` v1 → v2 load.
- `--physics-panel-smoke` with a vehicle in the scene; the Asset Editor's `.wvehicle` editor in a panel smoke.
- Each new check seen to fail under a negative control.

### Docs

New `docs/superpowers/documentation/vehicles.md`, a row in CLAUDE.md's area table, physics.md links to it, `docs/scripting.md` gets the `vehicle` API.

## Left to the builder

- Exact Jolt API mapping (units of the callback's slip arguments, how the flat curves combine with body friction, gear/clutch fields).
- Default tire coefficients and road-car tuning values.
- Command and snapshot field layout, file JSON keys.
- How the Asset Editor plots are drawn (ImPlot if vendored, else ImGui lines).
