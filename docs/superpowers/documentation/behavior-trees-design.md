# Behaviour trees and perception - design

> Historical design record, written before the area was built. Its status line may be out of date. What exists now is described in behavior-trees.md; where the two differ, that doc is right.

Status: draft for review. Branch `ai-nav` (or a branch cut from it). Sub-project 2 of the AI work; sub-project 1 (navigation, `navigation.md`) is built.

## Goal

AI that decides what to do with behaviour trees authored in the editor (`.wbt`), runs on an `AIController` component, walks with `NavAgent`, senses with `Perceiver`/`Perceivable`, and can be extended with Lua leaf nodes. Includes the editor to author it.

## Decisions (agreed)

- Leaves: built-in C++ nodes plus Lua leaves discovered as scripts and listed in the editor palette.
- First cut: runtime, node-graph editor, perception. **Not** in it: live node-state debugging in play, utility AI, environment queries, hearing occlusion, team communication, subtrees.
- Perception: `Perceiver` (sight and hearing) on the AI, `Perceivable` tags on targets, sound from `AI.emit_noise` in Lua. Results are written to the AI's blackboard on a sensing interval.
- Authoring: a `.wbt` editor in the Asset Editor, modelled on the `.wgraph` editor.
- Main thread only; runs only while the game clock plays; Editor Play runs on a clone and Stop discards it (repo rules).

## Asset: `.wbt`

- `AssetType::BehaviorTree`, appended before `Last`. Source and cooked form are the same JSON text (like `.wgraph`, `native_anim_files.h`): `BehaviorTreeData` holds the text, the runtime compiles it. `.wbt` is imported by `BehaviorTreeImporter` (parse + validate; a file that fails keeps the previous cook).
- Content: `root` (node id), `nodes` (id, type, name, `params`, `children` ordered), `blackboard` (key, type, default). Key types: bool, int, float, vec3, string, entity. Node ids are stable strings, never reused after deletion within a file (editor).
- Validation (shared by the importer, the editor and the smoke): a root exists; no cycles or shared children (a tree, not a DAG); child counts (composites 1+ except Parallel 2+; decorators exactly 1; leaves 0); unknown node type, unknown blackboard key in a node, a key used with the wrong type, a Lua leaf whose script is missing (warning, not an error: the node fails at run time and logs once). Each problem names the node.
- References: the Lua leaf scripts and animation clips a tree names are `AssetRef`s, listed as dependencies so the packer includes them.

## Nodes

All return `Success`, `Failure` or `Running`. A running node is ticked again next tick; aborting a running branch calls the abort of its running leaf.

- **Composites:** `Sequence`, `Selector` (both with `reactive`: re-evaluate earlier children each tick and abort the running one if an earlier child now wins), `Parallel` (policy: all succeed / one succeeds; failure policy likewise).
- **Decorators:** `Inverter`, `Succeeder`, `Repeat` (count, or forever), `Cooldown` (seconds, from the last finish), `Guard` (a blackboard comparison; false fails the child, and aborts it if running).
- **Built-in leaves:** `MoveTo` (target: a vec3 or entity blackboard key or a literal; stopping distance; `Running` while `NavAgent` is `Moving`, `Success` on `Arrived`, `Failure` on `NoPath`; stops the agent on abort), `Wait` (seconds, optional random range), `SetBlackboard`, `CheckBlackboard` (condition), `PlayAnimation` (clip by name through the `Animator`), `Log`.
- **Lua leaves:** a script that `extends` `BtAction` or `BtCondition`. Hooks: `on_start(self, bb)`, `on_tick(self, bb, dt) -> "success"|"failure"|"running"`, `on_abort(self, bb)`. `bb` reads and writes the blackboard (`bb:get(key)`, `bb:set(key, value)`), `self.entity` is the AI. Declared `properties` appear in the node inspector and are stored in the node's `params`. An error in a hook is logged once, the node fails, and the instance is disabled until the script reloads.

## Runtime (`engine/src/ai/`)

- `bt_compile.*`: `BehaviorTreeData` to an immutable `CompiledTree` (flat node array, resolved keys, parameters). Cached per asset in `AIModule`; a reimport recompiles and running instances restart.
- `bt_instance.*`: per-controller state (running child per composite, timers, repeat counts, cooldown stamps, Lua leaf `self` tables, the blackboard). Shared compiled trees, separate instances.
- `blackboard.*`: typed values with the declared defaults; `AIController` can override defaults; `Entity` values are held by guid (a destroyed entity reads as null).
- `ai_module.*`: `IModule`, priority 6 (after scripts 0 and `NavModule` 5, before animation 10), main thread. Registered in `Engine` before `ScriptingModule` (scripts ask for it). Follows the active scene while the game clock plays (like `NavModule`/`AudioModule`): controllers are made when the world starts or a component appears, removed with the component or entity. Ticks each enabled controller at its `TickRate` (default every frame). A destroyed or disabled controller aborts its running leaf.
- `AIController` component (`WI_COMPONENT`): `Tree` (asset), `Enabled`, `TickRate` (Hz, 0 = every frame), `Seed`, blackboard overrides (a flat list of key/value pairs by the same field machinery as script properties). Runtime state is not a field.
- Lua API (`bind_ai.cpp`): `AI.controller(entity)` returns a handle with `get(key)`, `set(key, value)`, `restart()`, `running()`; `AI.emit_noise(position, loudness, kind)`. Arguments are validated by the binding (the sol2 state has no safety defines: a wrong-typed argument must be a Lua error, never a crash; a method called with a dot is an error).
- Lua leaves are discovered through the script catalog by their base class; `ScriptInstances` is not reused for them (a leaf is not per entity script): `AIModule` owns a small Lua leaf host on the shared runtime and sandbox, with the same error rules as scripts.

## Perception

- `Perceiver`: `SightRange`, `SightAngle` (full angle, degrees), `EyeOffset` (local vec3), `SightLayerMask` (physics layers that block sight), `HearingRadius`, `Team` (string), `TargetTeams` (list; empty = all but its own), blackboard key names for the results, `Interval` (seconds, default 0.2).
- `Perceivable`: `Kind` (string tag), `Team` (string), `AimOffset` (local vec3, what the raycast aims at), `Visible` (bool).
- Sight, per sensing tick: for each `Perceivable` within range and cone of a `Perceiver`, a physics raycast from the eye to the aim point against the layers; sight holds if the ray reaches the target's collider or nothing blocks it. The nearest visible target wins. Uses the main-thread physics query snapshot (`PhysicsModule`), so a body made this frame is seen a step late.
- Hearing: `AI.emit_noise(position, loudness, kind)` queues a noise; each tick a `Perceiver` hears noises with `distance <= HearingRadius * loudness` (no occlusion). Noises expire after the tick that delivered them.
- Blackboard writes (names configurable on the `Perceiver`, defaults): `seen_target` (entity), `can_see_target` (bool), `last_seen_position` (vec3, kept after sight is lost), `heard_position` (vec3), `heard_kind` (string). Keys absent from the tree's schema are created at run time as untyped-by-schema entries the editor warns about.
- A `Perceiver` without an `AIController` still senses (a script can read it); with one it writes that controller's blackboard.
- Cost bound: perceivers sense on their own interval, staggered by entity id; at most `MAX_RAYS_PER_FRAME` raycasts per frame, the rest wait for the next frame (logged once if it keeps falling behind).

## Editor

- **BT editor** (`editor/src/asset_editor/` area, a new `BehaviorTreeEditor`, opened from the asset browser like the graph editor): node canvas (the existing node-graph widget used by `.wgraph`), a palette of built-in nodes and discovered Lua leaves with search, a node inspector (name, parameters; blackboard key pickers filtered by type), a blackboard table (add, rename, retype, remove, defaults), a validation list that selects the offending node. Operations through `BtEditModel` (no ImGui), snapshot undo like the graph editor (`edit_history.h`), save writes the `.wbt` atomically. Child order is the order top to bottom/left to right on screen, kept by an explicit "move up/down" and by drag.
- **Create menu:** "Behaviour tree" makes a minimal valid tree (a Sequence root with a Wait child).
- **Inspector:** `AIController` (tree asset slot, blackboard overrides listed from the tree's schema), `Perceiver`, `Perceivable` through the reflected fields.
- **Viewport gizmos:** sight cone and range, and hearing radius, for the selected `Perceiver` (through `DebugDraw`, a `GizmoSettings` switch like the audio ranges).

## Demo

A guard in the Navigation station: patrols between two points (`MoveTo` + `Wait`), switches to chasing the player-controlled entity (a `Perceivable`) on sight, goes to the last seen position when sight is lost, and investigates a noise made by a script. The tree is written by `scripts/gen_demo_scene.py` (`--ai-only`, like `--nav-only`: no downloads, no other `.meta` touched). `--demo-smoke` extends: the guard reaches its patrol point, then chases a target that was made visible. Per the demo-generator memory, back up the ignored `.meta` files before running the generator.

## Verification

- `--ai-smoke` (editor): `.wbt` parse/validate round trips and every validation error; each node semantic (sequence/selector memory and reactive abort, parallel policies, repeat, cooldown, guard abort, inverter, wait); blackboard types and overrides; `MoveTo` against a baked fixture navmesh (arrive, no path, abort stops the agent); Lua leaves (success, failure, running, abort, error disables); perception (cone edges, range, line of sight blocked by a wall, hearing radius and loudness, last seen kept, ray budget); the `AI` Lua API including wrong-typed arguments and dot calls; controller lifecycle (destroyed entity, removed component, clone and Stop leave nothing behind, recompile on reimport). Each with a negative control.
- `--ai-panel-smoke`: the editor for 130 frames with ImGui asserts on, building a small tree through the edit model and saving it.
- Smoke verdict is the `passed`/`FAILED` log line (the editor exe often crashes at shutdown).

## Risks and notes

- The area-cost change for navigation (named areas) is separate and not blocked by this.
- Lua leaf hosting is the novel piece: it must reuse the script runtime without becoming a per-entity script; the plan spikes it first.
- Sight raycasts depend on the physics query snapshot; the smoke uses the same physics world setup as `--physics-smoke`.
- Vector and entity blackboard values reach Lua as `vec3` / `Entity` handles; entities are guids, resolved on use.
- Out of scope, recorded so they are not mistaken for gaps: live debugging, utility AI, environment queries, subtrees, hearing occlusion, team communication, tree hot-swap of running state.
