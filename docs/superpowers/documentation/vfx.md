# VFX (particles)

Code: asset `engine/src/assets/particle_effect.*` (`.wfx`), component `ParticleSystem` in `engine/src/scene/components.h`, main-thread side `engine/src/scene/particle_module.*` and `particle_runtime.*`, GPU side `engine/src/renderer/particles/particle_pool.*`, passes `engine/src/renderer/passes/particle_sim_pass.*` and `particle_render_pass.*`, shaders `editor/engine/shaders/particle_*.hlsl` and `particle_*.hlsli`, importer `editor/src/asset_pipeline/importers/particle_importer.cpp`, editor `editor/src/asset_editor/vfx_*`, Lua `engine/src/scripting/bindings/bind_vfx.cpp` (user API: [docs/scripting.md](../../scripting.md)). Verified by `--render-test` (`tests/src/render/particle_checks.cpp`), `--vfx-editor-smoke`, `--script-smoke` and `--demo-smoke`.

## What it is

GPU particles. The CPU decides *how many* particles each emitter spawns in a frame; the GPU spawns, simulates, sorts and draws them, and the CPU never reads a particle back. An effect is a `.wfx` asset of up to `PARTICLE_MAX_EMITTERS` (8) emitters; a `ParticleSystem` component plays one in the scene.

**Implemented:** sprite emitters (camera, velocity, axis or world aligned; sprite sheets; additive, alpha or multiply blend; soft particles against scene depth; GPU back-to-front sorting; collision against the depth buffer). **Not drawn yet:** `ribbon` and `mesh` emitters (the file format, validation and editor have them; the sim pass logs once "Ribbon and mesh particle emitters are not drawn yet"), and `Lit` (reserved). Particles are not affected by shadows or decals and do not light the scene.

## The `.wfx` asset

JSON, `WFX_VERSION` 1; the cooked asset holds the same text, so reading it again gives the same tables. The grammar is in the comment above `write_particle_file` in `particle_effect.h`. Every module number is a `ParticleValue`: a constant, a random range `[a, b]` per particle, or a curve `{ "curve": [[t, v], ...] }` over the particle's life. Curves and the color-over-life gradient are baked into `PARTICLE_CURVE_SAMPLES` (32) sample tables when the file is read, so the GPU looks a table up. An emitter has:

- **Renderer:** `sprite | ribbon | mesh`, `material` (texture, emissive and alpha of the sprite), `sheet` columns/rows, `align`, `blend`, `softDistance`, `intensity` (above 1 glows through bloom), `sorted`, `maxParticles` (default 4096, up to 65536).
- **Spawn:** `looping`, `duration`, `delay`, `rate` (per second), `bursts` (time, count), `shape` (point, sphere, cone, box) with `shapeSize` and `coneAngle`, `worldSpace` (particles stay where they were born, else they move with the emitter).
- **Start values:** lifetime, speed, size, rotation, color.
- **Forces:** gravity, drag, noise strength and frequency.
- **Over life:** size, rotation speed, alpha, sprite frame, color gradient.
- **Collision:** `none | depth`, response `bounce | stick | kill`, restitution.

`particle_effect_valid` is the rule set of a playable effect (unique emitter names, 1..65536 particles, a sheet of at most 256 cells, positive duration and lifetime, finite numbers, keys rising within 0..1). The importer and the editor's Save enforce it; an invalid file fails the import and keeps the previous cook. `read_particle_file` never crashes on a bad file: it returns `nullopt` and a reason.

## Main thread

`ParticleModule` (priority 12, after the scripts) loads the effects it needs through `AssetManager` (and re-reads them when they are re-cooked), and while the game clock plays calls `tick_particle_systems`. For each `ParticleSystem` it keeps a runtime-only `ParticleRuntime` (not saved, not cloned): the system's clock (`TimeScale` applied), a seed, and per emitter the fraction of a particle carried over and the number to spawn this frame (`SpawnCount`, from the rate times `RateScale` times the project's `ParticleRateScale`, the bursts, and the time that passed). A step is at most 0.1 s (a stall does not spawn a flood). A system with `SelfDestruct` (made by `Vfx.spawn`) destroys its entity when it has finished. `ClearCount` counts up when the particles are to be discarded (`Vfx.stop` with clear, restart); the render thread starts the rings over when it changes.

`ParticleSystem` fields: `Effect`, `PlayOnStart`, `Looping` (false makes every emitter play once), `TimeScale`, `SortBias` (orders effects among those that blend), `ColorTint`, `SizeScale`, `RateScale`.

Editor behaviour: nothing plays in edit mode except systems with `ParticleRuntime::Preview` (the inspector's Play button, the VFX editor, thumbnails), which advance with real time; `ParticleModule::register_preview_scene` marks scenes that are only looked at, and `set_preview_effect` / `clear_preview_effect` show an unsaved effect in every system that names it (the VFX editor's edits, on the main thread and on the render thread, without a file).

## To the render thread

`SceneExtractor` sends `ParticleUpserts`, `ParticleRemovals` and `ParticleSpawns` in the `SceneDelta` (the system's matrix, effect, tint and scales, seed, clear count, and the spawn counts, which add up if several deltas arrive before a frame is rendered). `RenderScene` keeps a `RenderParticleSystem` per system with `PrevWorld`, so emitters inherit motion; `take_pending_spawns()` hands the counts to the simulation.

## GPU

`ParticlePool` (render thread only) is one big buffer of 64-byte `ParticleGpu` slots for the scene. Each emitter owns a **ring** of slots (`EmitterGpu` record, 320 bytes: matrix, ring, spawn data, shape, forces, the eight module values, render data; mirrored in `particle_common.hlsli`): a spawn writes the next slots, the oldest particle is overwritten when the ring is full (no free list), a slot whose age has reached its lifetime is dead, a zeroed slot is dead. The curve and gradient tables are in a table buffer, `PARTICLE_TABLE_ROWS` rows per emitter. Every view that draws particles has a pool of its own (`ViewFlags::Particles`, on in every profile, so previews and effect thumbnails show them; views.md).

- **`ParticleSimPass`** (once per frame in the main view, after the depth prepass and the decals; shaders `particle_spawn`, `particle_sim`, `particle_sort_fill`, `particle_sort_step`): gives every sprite emitter a ring, writes the emitter records, spawns what the main thread asked for, ages and moves every live particle (depth collision where the emitter asks: a particle within `COLLISION_THICKNESS` = 1 m behind the surface counts as touching it, a particle further behind passes through), and sorts the particles of blended emitters back to front on the GPU. It lists what the render pass draws (`ParticlePool::Draws`).
- **`ParticleRenderPass`** (after the blended forward items, before velocity): one draw per emitter, one instance per slot of its ring, into the HDR image, with no depth attachment: `particle_sprite.hlsl` reads the scene depth itself for soft particles. Three pipelines by `ParticleBlend` (alpha is premultiplied). It uses its own binding layout (particles, emitters, tables, depth), outside the frame/material/AO/skin sets (rendering.md).
- The velocity pass does not read particles: they are part of the frame's HDR content when TAA and motion blur run.

## Quality settings

`RenderQuality` (`render_quality.json`, Settings page, Renderer, group "Particles"): `ParticlePoolSize` (default 262144 particles, 4096..4194304, 64 bytes each), `ParticleMaxPerEmitter` (65536; 64..65536, caps every ring whatever the effect says), `ParticleRateScale` (0..4, multiplies every rate, not bursts: the lever for weaker machines), `ParticleCollision` (false: nothing collides with the depth buffer). Applied through the normal quality command to the render thread.

## Editor

- **Asset Editor** (`VfxEditor`, kind "Particle effect"): the emitter list (add, remove, duplicate, reorder, solo, mute), the modules of the selected emitter as fields, a curve view and gradient editor under them (`vfx_curve_view`), and a preview scene that plays the **unsaved** effect (`vfx_preview`). All edits go through `VfxEditModel` (undo/redo, dirty flag, validation); Save writes the file, the asset pipeline cooks it, and the systems that play it follow (the pool keeps their slots).
- **Inspector:** `ParticleSystem` from reflection; Play/Pause, Stop and Restart buttons run a preview in edit mode.
- **Thumbnails:** an effect's thumbnail is made by playing it in a preview scene until it looks fullest (`ThumbnailCache::step_effect_job`), then stopping the emission and rendering. A job ends after both 900 frames and 15 s of wall time.

## Lua

`Vfx.spawn{ effect, position, rotation, scale, parent, name, looping, time_scale, params, animator, joint }` makes a self-destructing entity (placed at an animator's joint when `animator` and `joint` are given; footstep dust uses this), and a placed system is driven with `Vfx.play`, `Vfx.stop(e, clear?)`, `Vfx.restart`, `Vfx.is_playing`, `Vfx.set_param` (`color`, `size`, `rate`, `time_scale`). Details in docs/scripting.md.

## Demo

Effects in `editor/assets/vfx/` (campfire, spark fountain, rune motes, footstep dust, impact sparks) with their materials and textures; the particles and decals demo (`demo_vfx.wscene`) places the campfire and the fountain, a script (`vfx_demo.lua`) spawns sparks, the motes float over the rune circle, and its HUD (`vfx_ui.lua`) puts the campfire out and on, sets the fountain's speed and spawns a burst; `vfx_footsteps.lua` puts dust at the footsteps of the warrior of the animation demo. Generated by `scripts/gen_demo_scene.py`; `--demo-smoke` checks that at least three placed effects are playing and that dust and sparks appear.

## Traps

- Particle data lives only on the GPU; nothing on the main thread may expect to read it.
- A new field of `ParticleEmitterData` must be added to `EmitterGpu` and `particle_common.hlsli` together (the static asserts pin the sizes), to the JSON reader/writer, `particle_effect_valid`, `VfxEditModel`, and baked in `bake()` when it is a curve.
- Changing `PARTICLE_CURVE_SAMPLES` changes the table layout (`PARTICLE_TABLE_ROWS`) and the cooked tables: bump the effect version handling and re-cook.
