# Animation assets B3: clip editor

> Historical design record, written before the area was built. Its status line may be out of date. What exists now is described in editor.md and animation.md; where the two differ, that doc is right.

Status: design, approved in conversation 2026-11-07. Builds on A (views), B1 (native `.wanim`) and B2 (Asset Editor panel, preview, undo helpers). C (animation graph) follows.

## Goal

Open a `.wanim` in the Asset Editor and edit it: a dope sheet and curves for the keys, pose keying in the preview, whole-clip operations, and events. The Animation panel's event editing is folded in.

## Decisions (from the brainstorm)

| Question | Decision |
| --- | --- |
| Where it lives | A third `IAssetEditor` (`ClipEditor`) in the Asset Editor panel; the Animation panel's event editing goes away. |
| Key editing | **Dope sheet plus Euler curves, with pose editing in the preview.** Rotation is edited as Euler X/Y/Z and stored as unit quaternions. |
| Widgets | **Custom dope sheet; `ImCurveEdit` from the updated vendored ImGuizmo** for the curves (not `ImSequencer`: it models bars with start/end, not keys on per-channel rows). |
| Whole-clip operations | **Core set:** trim, scale time, reverse, resample, reduce keys, make additive, rename, events. No mirror. |

A clip stores no loop flag or playback rate: those are play-time options (`PlayOptions`, `LayerOptions`), not clip-editor settings.

## Units

### `ClipEditModel` (`editor/src/asset_editor/clip_edit_model.*`, no ImGui)

Holds an `AnimationClipData` (channels by joint name and path, times, values, interpolation, events, duration, name, skeleton). History is snapshot undo (`EditHistory`, extended with a byte budget of about 64 MB besides the 100-step cap; the oldest step is dropped first). `dirty()` by revision as in the other models. A key is `(channel index, key index)`.

Invariants after every call: times finite and sorted per channel, keys inside `[0, Duration]`, rotation keys normalized, no channel empty (the last key of a channel cannot be deleted), event names valid and times inside the clip. A refused call changes nothing and makes no undo step; setting a value to what it is makes none either. Drags are one step (`begin_gesture`/`end_gesture`, as in `SkeletonEditModel`). Operations are made on a copy and swapped in only when they succeed.

Key operations: `move_keys(selection, dt)` (a key landing on another key of its channel replaces it; the selection cannot leave `[0, Duration]`), `scale_keys(selection, about, factor)`, `set_key_value`, `set_interpolation(selection, mode)`, `insert_key(channel, time)` (the sampled value), `delete_keys`, `copy_keys` / `paste_keys(playhead, target joint)` (relative times kept, matching channels of the same joint, or the selected joint when the copy came from one channel), `key_pose(joint, time, translation, rotation, scale)` (writes a key on the joint's three channels, creating a missing channel with the other keys at the rest pose of the skeleton).

Euler curves: rotation keys are exposed as Euler X/Y/Z degrees; editing one converts back to a quaternion choosing the Euler triple closest to the neighbour keys so that the curve does not flip. The file never stores Euler.

Whole-clip operations (one undo step each):
- **Trim [a, b]**: channels are sampled at `a` and `b`, boundary keys inserted, interior keys shifted by `-a`, `Duration = b - a`; events inside the range move with it, the rest are dropped. Refused unless `0 <= a < b <= Duration`.
- **Scale time by s / set duration**: key and event times multiplied; cubic-spline tangents scaled by the same factor. `s > 0`.
- **Reverse**: `t -> Duration - t`; cubic-spline tangents negated and swapped; events mirrored.
- **Resample at fps**: every channel sampled at `0, 1/fps, ...` up to `Duration`; rotation by slerp and renormalized; result linear. `fps` in 1..240.
- **Reduce keys (tolerance)**: drop a key when re-evaluating the channel without it stays within the tolerance (translation and scale in units, rotation in degrees), least-error first. Never drops a channel's first or last key.
- **Make additive (reference time)**: per joint, delta against the frame sampled at the reference time: translation subtracted, scale divided, rotation `q * inverse(q_ref)`. It bakes a delta clip (the runtime applies a clip as additive through `LayerOptions`); the clip carries no additive flag.
- **Rename**: the clip's name.
- **Events**: add, move, rename, delete (sorted by time).

### `ClipEditor` (`editor/src/asset_editor/clip_editor.*`)

The third `IAssetEditor`, for `.wanim`. It reads the file with `read_clip_file`, saves with `write_clip_file` and `write_file_atomic`.
- **Preview:** the existing `AssetPreview` poses the skeleton the clip names with the auto-picked mesh. The preview time and the dope sheet playhead are one value; play/pause and a scrubber. A clip whose skeleton has none of its joints stays editable (the preview says what binds).
- **Dope sheet (custom):** a time ruler with the playhead, a row per joint with a row per channel (translation / rotation / scale) inside, one diamond per key, an event lane. Joint rows collapse.
- **Curves (`ImCurveEdit`):** the selected joint's channels as X/Y/Z curves (rotation as Euler). Selected keys are shared between dope sheet and curves.
- **Selection and drag:** click, Shift/Ctrl add, box select across rows, click on a joint row selects its keys. Dragging moves in time, Alt-drag scales about the playhead, Snap (on by default, 30 fps grid) toggles frame snapping. Ctrl+C / Ctrl+V / Delete.
- **Pose keying:** a joint selected in the preview, the translate/rotate/scale gizmo on it, **Key** (K) writes a key at the playhead.
- **Events lane:** double-click adds, drag moves, F2 renames, Delete removes. The Animation panel's event editor and `open_events` / `save` for events are removed; its skeleton overlay and preview controls stay.
- **Whole-clip actions:** a toolbar menu; those with parameters open a small dialog (trim range, time scale or duration, resample fps, reduce tolerance, additive reference time); invalid input is refused in the dialog.
- Panel chrome as in B2: Undo/Redo, Save (Ctrl+S), `*` title, Save/Discard/Cancel, external-change bar. The browser opens `.wanim` here (double-click, context menu, Inspector).

### Vendored ImGuizmo update

Download the latest ImGuizmo (`ImCurveEdit.*`, `ImGuizmo.*`, later the node editor for C) into `editor/vendor/ImGuizmo`, make it build against the vendored ImGui 1.92 and keep the existing gizmo behaviour in the viewport and the Asset Editor. Only the files that are needed are compiled.

### Runtime and formats

None change: `.wanim` keeps its layout, `WASSET_VERSION` stays 4. Editing rewrites the file; the importer re-cooks it and the preview reloads through the existing path.

## Limits and out of scope

- Mirror L/R, retargeting, morph (weights) channel editing (weights channels are shown but not edited), curve tangent handles for cubic-spline (values and times only), multi-clip editing, recording from the game.
- Weights channels and any channel whose joint the skeleton lacks are kept untouched by every operation.

## Docs and verification

Update in the same change: `animation.md` (clip editor, operation semantics), `assets.md` (vendored ImGuizmo version), `editor.md` (Asset Editor clip editor, removal of event editing in the Animation panel), the CLAUDE.md area table if needed. Verification: Release build plus manual checks on Vulkan and D3D11 (no smoke tests added or run; the compile-only smoke sources stay compiling, adapted to the removed event API). Nothing is committed unless asked; spec and plan are never committed.

## Review focus

- Trim, reverse and scale time on cubic-spline channels (tangent handling), and on a clip with one key per channel.
- Rotation editing across the 180 degree wrap: no flips, quaternion stays unit and `q`/`-q` continuity kept between keys.
- Paste onto a channel of a joint the skeleton lacks; keys dropped onto existing keys; moving keys out of range.
- Resample or reduce on a clip with a zero duration, and on a channel with a single key.
- Undo budget: a dense clip (thousands of keys) does not exhaust memory; undo back to the saved state is clean.
- Pose keying when the clip has no channel for the joint or the preview skeleton is not loaded yet.
