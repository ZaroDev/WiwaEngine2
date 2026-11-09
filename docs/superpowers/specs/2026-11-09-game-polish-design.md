# Demo game polish: settings API, heroes, gating, drops, menus

Status: design approved 2026-11-09, not implemented. Not committed (spec/plan docs of this kind are not committed). Builds on `2026-11-09-demo-game-design.md` and `docs/superpowers/specs/game.md`.

## Goal

Fix what playing the first version showed, and turn it into a fuller game: models face the way they walk; the level cannot be skipped; items drop; both heroes use a resource; a main menu with real options; a hero select (one melee, one ranged). Real graphics options need a small engine API for scripts.

## A. Engine: settings API for Lua (`engine/src/scripting/bindings/bind_settings.cpp`)

- `settings.graphics.get()` returns `{ vsync, shadow_distance, shadow_cascades, bloom_mips, ao_slices, ao_steps, fxaa, taa }` from `RenderModule::get_quality()`. `settings.graphics.set(t)` merges the given keys, calls `RenderModule::set_quality` (sanitized, applied on the render thread, saved to `render_quality.json`). An unknown key or a wrong type is a Lua error naming it.
- `settings.scene.get()` / `.set(t)`: `bloom`, `ao`, `motion_blur` (bool) and `exposure_compensation` (number) of the active scene's `SceneSettings` through `Scene::set_settings`. Applies live, not persisted.
- `prefs.get(key, default)`, `prefs.set(key, value)` (bool, number or string; anything else is an error), `prefs.save()`: `user_prefs.json` next to the program, read once at start. A missing or unreadable file gives empty prefs and a log warning, never an error.
- Needs a way to reach `RenderModule` from the script context (as the debug draw already is). Not allowed from the render or physics threads: bindings run on the main thread only.
- Documentation: `docs/scripting.md`, `editor/engine/lua/wiwa.lua` stubs, `docs/superpowers/specs/scripting.md`; check in `--script-smoke` (prefs round trip in a temp file, graphics get/set round trip with the project file restored after, scene set).

## B. Level

- **Facing:** `Game.yaw_of` adds 180 degrees (the models face +z).
- **Gating:** `door.lua` (properties `group`, `needs_arena`) on a door entity: opens (sinks) when `Game.alive_in_group(group) == 0` and, if `needs_arena`, the arena has loaded. A `Courtyard gate` (group `courtyard`) closes the opening of the north wall; the existing `Gate door` (group `approach`) keeps `gate.lua`, which now loads the arena only when the approach group is clear. Doors are 6 m tall. Objective text: "Defeat all enemies to open the gate: N left". Falling still respawns at a checkpoint. The checkpoints and zones behind a door cannot be reached without opening it.
- Both doors stay open after opening; a restart closes them again (restart hook).

## C. Heroes, resources, drops

- `lib/heroes.lua`: table of heroes (`warrior`: model `Warrior`, melee, resource "Stamina", max 100, regen 30/s after 0.6 s, swing cost 25; `wizard`: model `Wizard`, ranged, resource "Mana", max 100, regen 10/s, bolt cost 20, no melee). `Game.hero` is the selected key; `Game.resource`/`Game.resource_max` are shared and shown in one HUD bar (`Hud/Resource`, `Hud/ResourceText`).
- `Player` has two model children (`Warrior model`, `Wizard model`); the unused one is scaled to 0. The Warrior plays its animation graph (as now); the Wizard crossfades clips (Idle, Walk, an attack clip) through `Animator` clip API.
- An attack with not enough resource is refused (the bar flashes: `Game.resource_flash`).
- `drops.lua` (pool `Drop 1..8`, entities placed parked like projectiles): `Game.drop(position)` on an enemy death. 60% health orb (heals 25, capped at max HP), 40% resource orb (restores 40). Orbs spin and bob, are collected within 1.4 m of the player, expire after 20 s (`time.total()`), and are cleared by restart. The boss drops none. Difficulty scales the health share.

## D. Menus and options (one HUD panel, new roots)

- Menu flow: `Title` (Play, Options, Quit) -> `Heroes` (two buttons with `ViewImage` portraits of the two `ViewHost`s, a line saying melee or ranged, Back) -> game. `Pause` gains Options and Main menu. Quit uses `quit()`.
- `Options` root with a tab row (Graphics, Controls, Game) and a Back button; each tab a container:
  - Graphics: Vsync (Toggle), Shadows (Slider on `shadow_distance` 20..150), Bloom, Ambient occlusion, Motion blur (Toggles), Anti-aliasing (button cycling Off, FXAA, TAA), Field of view (Slider 50..90).
  - Controls: one row per action (Move keys excluded: Jump, Melee, Fire, Pause) with a rebind button (conflicts shown, as now), Mouse sensitivity (Slider), Invert Y (Toggle), Reset to defaults.
  - Game: Difficulty (button cycling Easy, Normal, Hard: enemy damage x0.6/x1/x1.5, health drop share 0.75/0.6/0.4), Camera distance (Slider), Hints (Toggle: the objective text).
- Every change is applied at once and stored: graphics through `settings.graphics.set`, bloom/AO/motion blur through `settings.scene.set` and `prefs`, the rest in `prefs`; `prefs.save()` when leaving Options. At start `game_state.lua` reads the prefs and applies them (the scene settings of the level are the defaults when no pref exists).

## E. Verification

`--game-smoke` (selftest steps) is extended: facing (a model turned to a direction faces it, checked through the model's rotation), gating (door closed with enemies alive, open after they die, the bridge cannot be crossed first), drops (spawn, heal capped, expiry, restart clears), both heroes (resource spent, refused when empty, regen, hero select works), options round trip through `prefs` (a changed value read back after `prefs.save()`). `--script-smoke` covers the new API. `--demo-smoke` is run (its door-swing failure is pre-existing).

## Out of scope

Resolution and fullscreen (no Window hook), audio, save games, a persisted per-scene look of bloom/AO beyond `prefs`.
