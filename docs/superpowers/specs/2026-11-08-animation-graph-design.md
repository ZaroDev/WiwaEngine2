# Animation graph (C, stage one): state machines with layers

Status: design, approved in conversation 2026-11-08. Builds on A (views), B1 (native assets), B2/B3 (Asset Editor, clip editor). Fills the "Part 2" hook of `animation.md`.

## Goal

A `.wgraph` asset an `Animator` plays: parameters, layers, and per layer a state machine whose states play a clip or a 1D/2D blend space. Authored in a node editor in the Asset Editor, driven from Lua and tested with live parameter controls in the preview. The format and evaluator leave room for a later stage where a state's body is a general pose graph.

## Decisions (from the brainstorm)

| Question | Decision |
| --- | --- |
| Shape | **State machine now, pose-graph-ready.** A state body is a variant; stage two adds `PoseGraph`. |
| State body | **Clip or blend space** (1D/2D over float parameters). No sub-state-machines. |
| Layers | **In the graph**: each layer is a state machine with a mode (base, override with mask, additive) and a weight. Script layers (`add_layer`) keep working beside them. |
| Transitions | **Conditions plus optional exit time**, crossfade duration, `Any` source. No interruption rules, no curves. |
| Runtime | **Compile into the existing pose nodes** (`StateMachineNode`, `BlendSpaceNode`), not a second evaluator. |

Out of scope: root motion, IK, sub-state-machines, interruption rules, transition curves, a general pose graph.

## Asset: `.wgraph`

- Authoring file: JSON with a version, read and written by `engine/src/assets/native_anim_files.*` (the one description of the format). Cooked into a new `AssetType::AnimationGraph` (append before `Last`); `WASSET_VERSION` becomes 5.
- Contents:
  - `skeleton`: the skeleton it was made for.
  - `parameters`: `{name, type: float|int|bool|trigger, default}`, names unique.
  - `layers`: `{name, mode: base|override|additive, weight (constant or a float parameter), mask, machine}`. The first layer is the base layer and has no mask. A mask is a list of `{joint, weight}` or a root joint with a falloff.
  - `machine`: `default` state; `states` `{name, speed (constant or parameter), loop, body, position (editor only)}`; `transitions`.
  - `body`: `{clip: AssetRef}` or `{blend: {x: param, y: param?, samples: [{clip, x, y}]}}`. 1D blends the two nearest samples; 2D uses gradient bands over the samples (continuous everywhere; a triangulated blend jumped where its triangles overlapped).
  - `transition`: `{from: state|Any, to, conditions, duration, exitTime?}`. A condition is `{parameter, op, value}`; ops `> < >= <= == !=` for numbers, `true`/`false` for bools, `fired` for triggers. All conditions must hold. An exit time is a fraction of the source state's length.
- Import validation (an error with the reason): unknown parameter or state name, duplicate names, an empty layer, a missing default state, a blend space without samples. A clip that does not fit the skeleton is a warning, as for clips today.

## Runtime

- `AnimatorInstance` gets a graph mode: on load it compiles each layer into a `StateMachineNode` (and a `BlendSpaceNode` per blend-space state) and binds clips once. Compiled layers use the same masked/additive layer path as `add_layer`.
- `StateMachineNode`: active state, optional outgoing state and fade progress. Each advance it moves the state time, reads the parameters, and checks the transitions from the active state then from `Any`, in file order; the first whose conditions hold fires. A transition with an exit time waits until the active state's normalized time reaches it. A transition that fires mid-fade snapshots the current blended pose as the outgoing side and starts a new fade from there. Triggers are consumed by the transition that uses them, or cleared at the end of the frame if none fired.
- `BlendSpaceNode`: evaluates its samples' `ClipNode`s with weights from the parameters; time is shared and normalized so footsteps stay in step.
- Events keep the rule "fire only for a clip whose effective weight is above 0.5"; the state machine passes effective weights down.
- Parameters live in a flat array per instance (graph order) with a name-to-index table. They reset to defaults on Play and on a graph reload. Main thread only; the asset is CPU-side and `Animator` stores an `AssetRef`.

## Component and Lua

- `Animator.Graph` (`AssetRef`, a `WI_FIELD`, so serializer, inspector, overrides, clone, Lua and stubs follow). With a graph set the Animator plays it from the scene start and `Clip`/`PlayOnStart` apply only to an Animator without one. A graph whose skeleton differs from the Animator's is refused once with a log line and the Animator falls back to its clip.
- Lua: `anim:set(name, value)` (number or bool), `anim:get(name)`, `anim:trigger(name)`, `anim:state(layer)`. An unknown name or wrong type logs once and is ignored (safe every frame). `play` and `crossfade` on a graph Animator leave graph mode (logged once). LuaLS stubs and `docs/scripting.md` get the calls.

## Editor

- `GraphEditor` (`IAssetEditor`, `editor/src/asset_editor`) opens `.wgraph` in the Asset Editor: preview left, node canvas in the bottom pane (vendored `GraphEditor` widget; states are nodes, transitions links, Any and Entry fixed; layer tabs above), tool pane right.
- Tool pane: parameters (add, rename, delete), selected state (body, speed, loop; blend samples table), selected transition (conditions, duration, exit time, order), selected layer (mode, mask, weight).
- Preview plays the saved graph. It has a live control per parameter (slider, checkbox, Fire button for triggers) and a readout of the active state per layer.
- `GraphEditModel` (no ImGui): snapshot undo through `EditHistory`, one step per call, a drag one step, a refused call changes nothing and sets `error()`. Renaming a parameter or state rewrites every reference; deleting a used parameter is refused and lists where.
- Asset Browser: icon, thumbnail, tooltip summary, "New Animation Graph" in the context menu (one layer, one empty state).

## Checks

- `--anim-smoke`: `GraphEditModel` (rename propagation, refused delete, file round trip), cooking (a bad reference fails import), evaluator (a parameter change takes a transition at the right frame; exit time waits; the crossfade ends at the target pose; an interrupted fade is continuous; a trigger is consumed once; a 2D blend space gives the sample's pose at a sample point; a layer mask applies). Each is seen failing under a negative control.
- `--anim-panel-smoke`: also opens the graph editor for 130 frames with ImGui asserts on.
- Demo: the warrior in `demo.wscene` uses a graph (idle, walk, run as a blend space on `speed`; a trigger-driven attack on an upper-body layer) driven by a demo script; `--demo-smoke` covers it (`scripts/gen_demo_scene.py`).
- Docs: `animation.md`, `assets.md`, `editor.md`, `scripting.md`, `docs/scripting.md`, CLAUDE.md smoke table.

## Later stage

A state body gains a `PoseGraph` variant (a node graph ending in an output pose) with no change to the file's top level or to `StateMachineNode`.
