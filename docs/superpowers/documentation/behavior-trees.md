# Behaviour trees and perception

Behaviour trees (`.wbt`) authored in the Asset Editor, run by an `AIController`, walking with `NavAgent` ([navigation.md](navigation.md)), sensing with `Perceiver` / `Perceivable`, extended with Lua leaves. Sub-project 2 of the AI work.

## Shape

- **Format and validation (engine, `engine/src/ai/`):** `bt_types.h`, `bt_file.*` (`.wbt` JSON read/write, the node catalog `bt_builtin_node_specs`, `validate_bt`). The text is also the cooked form (`BehaviorTreeData`, `AssetType::BehaviorTree`, appended before `Last`); `BehaviorTreeImporter` (editor) parses and validates it and fails the import on an error (the previous cook stays), warnings are logged.
- **Runtime:** `blackboard.*` (typed values), `bt_compile.*` (`CompiledTree`, immutable, shared), `bt_instance.*` (per-controller node state + blackboard; the node semantics live in `BtInstance::run`), `bt_leaves_engine.*` (`MoveTo`, `PlayAnimation`), `bt_lua_host.*` (Lua leaves), `perception.*`, `ai_module.*` (`AIModule`).
- **Editor:** `editor/src/ai/` (`bt_edit_model` no ImGui, `bt_canvas`, `bt_editor`), hooked into `AssetEditorPanel` for `.wbt`; `perception_gizmo`.
- **Threads and clock:** main thread only; `AIModule` priority 6 (after the scripts 0 and `NavModule` 5, before the animation 10); runs only while the game clock plays; follows the active scene like `NavModule` (Editor Play = a clone, Stop aborts what runs once).

## The file

`{ "version": 1, "root": id, "blackboard": [{ key, type, default }], "nodes": [{ id, type, name, params, children }] }`. Key types: bool, int, float, vec3, string, entity (a 32 hex guid or null). The editor keeps node positions in the `ui` param. `validate_bt` (shared by the importer, the editor and the checks) reports: no/unknown root, duplicate ids, a child that does not exist, a node reached twice or in a cycle, the wrong child count, an unknown type, a param of the wrong kind or missing, an undeclared or wrongly typed blackboard key, a comparison that does not fit the key type, duplicate/empty keys, a default that does not fit. Nodes the root does not reach and unknown params are warnings. It does not check that a Lua leaf's script exists (the engine validator has no asset access): the editor shows it, the runtime fails the node.

## Nodes

All return Success / Failure / Running. Sequence and Selector have a `reactive` mode (re-evaluate from the first child each tick; the running child is aborted when an earlier one decides). Parallel: `success` "all"/"one", `failure` "one"/"all"; when every child has finished and no policy matched it is Success if any succeeded. Decorators: Inverter, Succeeder, Repeat (`count`, 0 = for ever), Cooldown, Guard (comparison on a key; false fails the child and aborts it if running). Leaves: Wait (`seconds`, or `min`/`max` drawn from the instance's seeded generator), SetBlackboard, CheckBlackboard, Log, MoveTo, PlayAnimation, LuaAction, LuaCondition.

- **External leaves** (`BtKind::External`) are asked through `BtServices::Leaf(BtLeafCall)` with phases Start (its answer is the first tick's), Tick, Abort. `AIModule` routes MoveTo/PlayAnimation to `bt_engine_leaf` and Lua leaves to the host.
- **MoveTo** (`target` a point or `key:<name>` of a vec3 or entity key, `stoppingDistance`): Running while the `NavAgent` is Moving, Success on arrival or within the stopping distance, Failure for a missing/empty/gone target, no NavAgent, no navmesh, no route, or an agent someone else stopped; one log per node run. An entity target is followed (destination set again when it moved more than a metre). **Ownership trap:** a reactive switch aborts the old branch *after* the new one has run, so an abort only stops the agent if that node still owns the walk (`BtInstance::move_owner`).
- **PlayAnimation** goes through hooks (`PlayClip`, `ClipBusy`) so checks run without the animation module.

## Blackboard and controllers

`Blackboard::set` refuses an unknown key, a value of another type (no conversions) and NaN floats/points; `set_dynamic` makes undeclared keys (perception does). `AIController { Tree, Enabled, TickRate (Hz, 0 = every frame), Seed, Overrides }`: `Overrides` is a JSON object text of key to value (the reflection layer has no list-of-pairs field). A controller with an unset, loading, failed or invalid tree is skipped (logged once); a changed tree (`register_tree`, or an asset reimport noticed through the asset manager's reload listener) restarts the controllers that use it, aborting the old leaves once. Disabled, removed, destroyed or stopped: the running leaf is aborted exactly once. `AIModule::acquire(entity)` makes an instance on demand (scripts run before the module ticks).

## Lua

- `AI.controller(entity)` (an error without an `AIController`) returns a handle with `get`, `set`, `restart`, `running`; `AI.emit_noise(position, loudness, kind)`. Every argument and the `self` of every method is checked by the binding: sol2 safeties are on in every build (`SolDefines()`, scripting.md), so a wrong-typed argument or a call with a dot is a Lua error, and the binding still checks them itself to name the argument.
- **Leaves:** a script returns `BtAction:extend{ properties, on_start(self, bb), on_tick(self, bb, dt), on_abort(self, bb) }` (or `BtCondition`); the bases are defined by `define_bt_lua_bases`, called from `ScriptRuntime`'s constructor (so the script importer's runtime knows them) and marked with an inherited `__bt_kind`. `on_tick` returns "success" / "failure" / "running"; anything else, an error in a hook, a missing script, a script that is not a leaf or of the wrong kind fails the node with one log (no persistent disable: it tries again next run). Properties come from the script's `properties` with the node's params over the defaults; `self.entity` is the AI; `bb:get/set/has` are typed and errors name the key and the type. Leaf scripts are skipped by `ScriptingModule::register_catalog_scripts` (not entity components); the editor's own registration on import still lists them.
- `ScriptingModule::shutdown` detaches the host (`AIModule::detach_lua`, which aborts everything while the Lua state is alive) before the runtime goes.

## Perception

`Perceiver` (sight range/angle, eye offset, layer mask, hearing radius, team and target teams, interval, five blackboard key names) and `Perceivable` (kind, team, aim offset, visible). Sight per perceiver every `Interval` (first pass at once): targets in range and cone, one raycast each (nearest-first shortcut), blocked by any body but the target's own and the perceiver's; at most `MAX_RAYS_PER_FRAME` (64) rays a frame, deferred perceivers go first next frame. Hearing is checked every call (a noise lasts one tick): `distance <= HearingRadius * loudness`, the nearest noise wins. Results go to the controller's blackboard (`seen_target`, `can_see_target`, `last_seen_position` kept after sight is lost, `heard_position`, `heard_kind`); a perceiver without a controller still senses (`AIModule::perception().result(entity)`). The sight raycast is injected (`SightRaycast`): production uses `PhysicsModule::query()->raycast_all` (a body made this frame is seen a physics step late), checks use boxes. `AI.emit_noise` queues for the tick.

## Editor

`BehaviorTreeEditor` (Asset Editor panel, `.wbt`): the canvas in the big column (nodes laid out top-down when they have no `ui` position; drag from an output slot to a node to move it under that child position, pick a link up to remove the child and what is below it, right click for add child / move earlier-later / rename / delete); the tool pane has the selected node's name and params (key pickers filtered by type, Lua leaf picker with its properties), the blackboard table (retyping or removing a key a node uses is refused naming the nodes, with "Do it anyway"), and the problem list (click selects the node). `BtEditModel` is the document (undo/redo by snapshots, gestures, ids never reused, `Save` refused while there is an error). Create menu: "Behaviour tree". Viewport: `GizmoSettings::Perception` draws the sight cone and hearing sphere of the selected `Perceiver`.

## Demo

The guard of the navigation and AI demo (`demo_ai.wscene`): patrols east of the wall, sees the thief across the arena and chases it, looks at the crash the noise maker makes. Its HUD (`ai_ui.lua`) reads the guard's blackboard (`AI.controller`), makes a noise (`AI.emit_noise`) and moves the thief. `--demo-smoke` checks the guard has a controller, saw the thief, heard the crash and came within 3 m of the thief.

## Verification

`--ai-smoke`: format, every validation rule, archive, every node semantic (with a scripted fake for External leaves), blackboard, MoveTo against a baked navmesh (arrive, follow, unreachable, destroyed target, abort, stopping distance, reactive switch), controllers (lifecycle, tick rate, recompile, clones), Lua leaves (all failure modes, reload, properties), the AI API (types, errors, dot calls, stubs), perception (cone, range, wall, teams, hearing, budget with 300 perceivers), the edit model, gizmo shapes and settings. `--ai-panel-smoke`: the editor for 120 frames with ImGui asserts, a tree built through the model and saved by the panel. Each check was seen to fail under a negative control. The verdict is the log line: the editor exe often crashes at shutdown.

## Traps

- Not covered by a smoke: mouse interaction on the canvas, the Inspector rows of each param kind beyond drawing, the viewport drawing of the gizmo (only its shapes), a reimport of a tree asset while playing.
- Leaf scripts still show up in Add Component in the editor.
- `scene_components.cpp` needs `/bigobj` (premake filter) now.
- `near` / `far` are Windows macros (they bit a smoke twice).
- Out of scope, so it is not mistaken for a gap: live node-state debugging in play, utility AI, environment queries, subtrees, hearing occlusion, team communication.
