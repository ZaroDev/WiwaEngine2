# Demo game: "Warrior's Crossing"

Status: design approved 2026-11-09, not implemented. Not committed (spec/plan docs of this kind are not committed).

## Goal

A short, playable third-person action game that uses the engine's systems together in real gameplay: Forward+ rendering and post, Lua scripting, Jolt physics (character, queries, joints, triggers), skinned animation with animation graphs, game UI, input actions and rebinding, views, materials and additive scene loading. It is separate content from `demo.wscene` (the feature tour) and stays playable end to end: title, play, win or lose, restart.

## Level (Option C, hybrid)

- `level.wscene` (default scene of the game build): **Courtyard** (tutorial prompts, Yetis and Pandas) and **Bridge** (swinging and breakable plank joints, a sliding platform, a kill zone below, a checkpoint at the end).
- `boss_arena.wscene`: loaded additively by a gate, with its own SceneSettings (dusk, fog, bloom, red pulsing light). The Yellow Dragon boss fight; win on its death.
- Checkpoints are trigger volumes that store the respawn point.

## Gameplay

- Player: Warrior with a `CharacterController`, Move and Jump actions, spring-arm follow camera. Animation graph parameters `speed`, `grounded`, trigger `attack`.
- Melee: `physics.overlap_sphere` in front of the player; knockback with `body():add_impulse`.
- Ranged: projectile from a pool of 12 pre-placed rigid-body spheres, with a cooldown.
- Enemies: Yeti (fast, light) and Panda (slow, heavy). State machine chase/attack/dead, HP, knockback, world-anchored HP bar.
- Boss: Yellow Dragon, three HP phases (charge, projectile volley, wakes pooled Yetis).
- No prefab-instantiate or spawn API exists in Lua: enemies and projectiles are **pre-placed pools** of inactive entities that scripts move and reactivate.

## Content and generation

- `scripts/gen_game.py`, modelled on `gen_demo_scene.py`: writes `editor/assets/game/` (both scenes, Lua scripts, HUD and menu `.wuipanel`, `.winput` map), reusing the existing models, materials and sky. GUIDs derive from paths. `.meta` files are force-added.
- `editor/build_settings.json` default scene becomes `level.wscene`. `demo.wscene` is untouched.

## Scripts

`player.lua`, `enemy.lua`, `boss.lua`, `game_state.lua` (Title, Playing, Paused, Dead, Won; checkpoints; kill zone; objective text), `bridge_*.lua` (breakable planks, slider), `gate.lua` (`load_scene("boss_arena", "additive")` with a progress bar, then opens the door), `boss_light.lua`.

## UI and input

HUD: player HP, ranged cooldown pip, objective, boss bar (hidden until the fight), Warrior `ViewHost` portrait. Title, pause (with rebinding list), win and game-over screens, each with Restart. `Gameplay` and `UI` action maps are gamepad-ready and rebindable.

## Verification

- `--game-smoke` (editor): scripted run that loads the level, takes damage and respawns at a checkpoint, kills an enemy with both attacks, triggers the boss scene load, and wins. Must be seen to fail under a negative control. `--demo-smoke` must stay green.
- Add `docs/superpowers/specs/game.md` and a line in the CLAUDE.md area table.
- Build and verify in Release; run smokes only when asked.

## Out of scope

Audio (the engine has none), save files, multiple weapons, new engine features. Any engine gap met while scripting is flagged, not silently worked around.

## Open risks to check first in the plan

Additive load of a scene with its own SceneSettings (which settings win), pooled-entity activation from Lua (enable/disable component or move offscreen), and whether `ViewHost` works inside a HUD canvas.
