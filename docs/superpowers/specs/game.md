# Demo game: Warrior's Crossing

Code: `scripts/gen_game.py` (writes `editor/assets/game/**`), Lua sources in `scripts/game_scripts/` (copied by the generator), smoke `editor/src/play/game_smoke.*`. Verified by `--game-smoke`. Run `scripts/gen_demo_scene.py` once first: the game reuses its models, materials, sky and font by GUID (GUIDs derive from asset paths).

## What it is

A short third-person action game: a courtyard with Yetis and Pandas, a hinged plank bridge over a pit, a platform sliding over a second pit, a gate that loads the boss arena (`boss_arena.wscene`) additively, and the Yellow Dragon in three phases. Warrior with sword (`Melee`) and shots (`Fire`), checkpoints, pause menu with rebinding, death/respawn, win/restart. Default scene of the build is `game/scenes/level.wscene`; the demo scene stays in the build.

## Structure

- `lib/game.lua` is the shared state (a `require`d module runs once): game state machine value, HP, checkpoint, enemy registry, restart hooks, teleport queue. `game_state.lua` owns flow, HUD and menus; every other script only calls `Game.*`.
- No spawn API exists in Lua, so everything is **pre-placed**: enemies (`enemy.lua`, also the boss with `boss = true`, and dormant arena Yetis the boss wakes in phase 3), projectile pools (`projectiles.lua`: `Projectile 1..12` for the player, `Fireball 1..10` for the boss).
- Projectiles are not rigid bodies: each step they sweep with `physics.raycast_all` and park themselves on a hit or after `life` seconds (`time.total()`, so dropped physics steps do not stretch it).
- Entities have no enable flag. A "removed" enemy loses its `CharacterController` and its model scales to 0. `Game.teleport` does the same dance in two frames (remove the controller, next frame set the position and add it back): the physics world places a character where its component is made.
- Enemy and player knock-back is a decaying velocity added to `character.move` (rigid-body impulses do not apply to characters).
- The HUD is one panel (`game/ui/game_hud.wuipanel`): roots `Hud`, `Objective`, `Boss`, `Bar1..6` (world-anchored enemy bars, anchored by the enemy script), `Portrait` (ViewImage for the `Warrior portrait` ViewHost), `Loading`, and the menus `Title`, `Pause`, `Dead`, `Won`.
- The arena's mood comes from `boss_light.lua` (dims the level's `Sun`, pulses a red light), not from the arena's SceneSettings: with an additive load the first scene's settings are the ones that apply.

## Heroes, resources, drops, gating (second pass)

- Two heroes chosen on the hero select screen (`Game.select_hero`): the Warrior (melee, stamina, sword) and the Wizard (ranged, mana, bolts). Both are children of `Player` (`Warrior model`, `Wizard model`); `player.lua` shows the selected one and scales the other to 0. The Warrior uses his animation graph, the Wizard crossfades clips (`wizard_*` properties, read from the FBX by the generator). Fire and Melee both trigger the hero's attack; an attack without enough resource is refused and the HUD bar flashes.
- Models face **+z** (`Game.yaw_of`).
- Doors (`door.lua`, kinematic bodies that sink by writing their transform) stay shut until every enemy of their group is dead: `Courtyard gate` (group `courtyard`) in front of the bridge, `Gate door` (group `approach`, `needs_arena`) in front of the arena. `gate.lua` loads the arena only when the approach is clear and the hero stands at the trigger. Restart shuts the doors again.
- Drops (`drops.lua`, pools `Heart 1..6` and `Orb 1..6`): an enemy that dies leaves a health orb (+25 HP, capped) or a resource orb (+40) by the difficulty's health share; they spin, are collected near the hero and expire after 20 s (`time.total()`).
- Menus live in `lib/menus.lua`: title (Play, Options, Quit), hero select, pause (Resume, Options, Restart, Main menu), dead, won, and Options with Graphics / Controls / Game tabs. Options are applied at once and stored: graphics through `settings.graphics` (render_quality.json) and `settings.scene`, everything else through `prefs` (`user_prefs.json`, saved when Options is left). The widgets the menus look for are checked by the self test (`Game.ui_missing`).
- Engine API added for this: `settings.graphics`, `settings.scene`, `prefs` (`bind_settings.cpp`, `core/user_prefs.*`, docs in `docs/scripting.md`, stubs in `editor/engine/lua/wiwa.lua`, checked in `--script-smoke`).

## Scenes and flow

- `main_menu.wscene` is the **default scene** of the build: a dedicated diorama (the two heroes on a rug between braziers, the bridge into the distance, the gate and the Yellow Dragon over it), `menu_camera.lua`, `menu_state.lua`, and two UI canvases: `Menu` (`main_menu.wuipanel`: Title, Heroes, Loading) and `Options` (`options.wuipanel`). Play -> hero select -> `load_scene("assets/game/scenes/level.wscene", "replace")`; the hero travels in `prefs` ("hero", not saved to the file).
- `level.wscene`: the game. Canvases `Hud` (`game_hud.wuipanel`: HUD, Pause, Dead, Won) and `Options`. The pause, dead and won screens have "Main menu", which loads the menu scene. `boss_arena.wscene` is still loaded additively behind the gate.
- Shared Lua: `lib/game.lua` (state), `lib/menus.lua` (widget lookup, accent bars, screens), `lib/options.lua` (the Options screen, used by both scenes: it opens over whatever state the game is in and `close` returns to it).

## Main menu design

Spec: `2026-11-09-main-menu-design.md`. Brass and plum at golden hour: Almendra SC (downloaded by `gen_game.py`, falls back to Lato) for the title and headings, Lato for everything else. Title and pause are a left column of text rows over a stepped dark band (UI has no gradients); hero select puts a card (role and three stat bars) under each stage hero; Options is a right-hand drawer; Dead and Won are one big word. `menus.lua` shows each row's brass `Accent` bar on hover and focus. The menu camera has two poses (a slow arc on the title, a front view for the hero select) and blends 0.9 s between them. `UiKit` in `gen_game.py` holds the palette and the building blocks of all three panels.

## Input

`editor/input.wsettings` points at `game/input/game.winput`, a superset of the demo's map (same `Gameplay` and `UI` actions, plus `Melee` and `Pause`; Fire is the left mouse button, Melee is E and the right mouse button, and either attacks with the selected hero). Running `gen_game.py` rewrites both `input.wsettings` and `editor/build_settings.json`.

## Smoke

`--game-smoke` opens the **main menu scene**, plays it and raises the signal (`SmokeSignal` entity, `Transform.Position.x = 1`) for `menu_selftest.lua`, which checks the title (every widget path resolves, the heroes and the dragon play their animations), the Options screen, the hero select camera, then chooses the Wizard like the button does and loads the level with the flag `prefs` "smoke". `selftest.lua` of the level then checks the hero arrived and walks the game through: start, walk, melee, ranged (and an exhausted pool), death and respawn, pause freezes enemies, a plank breaks when hit (and the bridge must not break by itself), the platform slides, the gate loads the arena once, boss phases and minions, one win only, restart resets. It logs `GAMESMOKE PASS` or `GAMESMOKE FAIL: <step>: <why>`; the C++ side waits for that line and fails on any `[error]` in the log. The self test drives the game through `Game.*` and `Game.fake_move`, not through the input devices.

## Traps

- Entity names and HUD widget paths are used by `selftest.lua` and the scripts; change them together with the generator.
- Bridge planks must be light (10 kg): at 1.2 tonnes each a nearly taut chain breaks its own joints at start. Joint anchors are in world units (not scaled by the entity), and neighbouring pieces must have the same thickness so the anchors meet.
- A kinematic platform is moved by writing its transform (the demo does the same); `set_kinematic_target` did not move it.
- `--script-smoke` fails on `joint_scripts_motor`, `joint_scripts_errors` and `joint_scripts_component` (joint motor tests, unrelated to this work; same class of timing failure as the demo door below).
- `--demo-smoke` fails on the door-swing check in a Release build run at unlimited frame rate (pre-existing, same with the original `input.wsettings`).
- The first frames log `No view named 'warrior_portrait'` (the HUD asks before the ViewHost registers); it is a warning and the portrait appears afterwards.
