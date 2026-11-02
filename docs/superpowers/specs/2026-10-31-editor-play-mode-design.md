# Editor play mode — sub-project 2 of 3

Status: design approved in conversation, awaiting written-spec review.

Builds on `2026-10-31-script-runtime-design.md` (sub-project 1). Sub-project 3 (hot reload) is separate.

## Goal

A Play / Pause / Step / Stop control in the editor that runs the scene's scripts, so that nothing a script does can damage the scene
being edited.

Decisions taken in the brainstorm:
- **Play runs on a clone of the edit scene**, not on the edit scene. Stop discards the clone and re-activates the untouched edit scene.
  (Snapshot-and-restore through the scene-record layer was rejected: it does not save `PrefabChild`, so generated prefab children would
  come back as ordinary entities.)
- **Panels stay live while playing** (Unity-style): the Hierarchy and Inspector show the running clone and can inspect and change it;
  those changes disappear on Stop. The viewport keeps the editor camera and gizmos. Saving and opening scenes is disabled while playing.
- **Controls:** Play, Pause/Resume, Step (while paused), Stop.

## Bug fixed on the way (sub-project 1 shipped it)

`Time::init()` starts only the real-time and engine clocks. The game clock, which `ScriptingModule` takes `dt` from, is never started
(`IsPlaying` false, `TimeScale` 0), so in a built game every `on_update` receives `dt = 0`. `Clock::start()` also never sets `LastTime`,
so the first delta after a start is the time since boot. Fixes: `Clock::start()` sets `LastTime = Timer`; `Time::init()` starts the game
clock (a game runs by default); the editor calls `stop_game()` after init (edit mode has no game time) and `play_game()` on Play.

## Units

| Unit | Purpose | Depends on |
|------|---------|-----------|
| `clone_scene` (`engine/src/scene/scene_clone.h/.cpp`) | `Ref<Scene> clone_scene(Scene& source)`: deep copy. Same entity guids, hierarchy and sibling order, root order, all reflected components (through the ComponentRegistry), `ValueBags`, `PrefabInstance` with its overrides, `PrefabChild`, `UnknownComponents`, scene settings. World transforms are recomputed. | Scene, ComponentRegistry |
| `IPlayHost` + `PlayMode` (`editor/src/play/play_mode.h/.cpp`) | State machine `Edit` / `Playing` / `Paused`: `play()`, `pause()`, `resume()`, `step()`, `stop()`, `update()` (once per frame, in the editor's `post_update`), `state()`. All effects go through `IPlayHost`, so the logic is tested with a fake host. | none besides the interface |
| `EditorPlayHost` (in `editor_module.cpp`) | The real `IPlayHost`: SceneEditor, SceneModule, ScriptingModule, Time. | the above |
| `SceneEditor` additions | `set_playing(bool)` (while on, `new_scene`, `open`, `save`, `save_as` refuse and return false), `restore_document(Ref<Scene>, path, dirty)` (puts back what Play replaced). | — |
| `ScriptingModule` change | `update()` runs the scripts only while the game clock is playing (not paused). | Time |
| Toolbar | Buttons in the existing empty `##SecondaryMenuBar`: Play/Stop, Pause/Resume, Step. Pause and Step are disabled when they do not apply; the bar is tinted while playing; Play is disabled while a scene is opening or a prefab expansion is pending. | PlayMode |

### `IPlayHost`

```cpp
class IPlayHost
{
public:
	virtual ~IPlayHost() = default;
	virtual Ref<Scene> edit_scene() = 0;                       // the scene the editor shows now
	virtual bool can_play() = 0;                               // false while a scene is opening or a prefab expansion is pending
	virtual EditorDocument capture_document() = 0;             // path, dirty flag, selected entity guid
	virtual void show_scene(Ref<Scene> scene, bool playing) = 0; // makes it the SceneModule's active scene and the editor's, sets set_playing
	virtual void restore_document(Ref<Scene> scene, const EditorDocument& document) = 0;
	virtual Ref<Scene> active_scene() = 0;                     // the SceneModule's active scene now
	virtual void start_scripts() = 0;
	virtual void stop_scripts() = 0;
	virtual void time_play() = 0;  virtual void time_stop() = 0;
	virtual void time_pause(bool paused) = 0;
};
```

## Flow

- **play()** (only from `Edit`, and only if `can_play()`): remember `edit_scene()` and `capture_document()`; clone it; `show_scene(clone,
  true)`; `time_play()`; `start_scripts()`; state `Playing`.
- **pause() / resume()**: `time_pause(true/false)`; state `Paused` / `Playing`. The editor, viewport and gizmos keep running.
- **step()** (only in `Paused`): sets a request. In the next `update()` (editor `post_update`): `time_pause(false)`. In the following
  `update()`: `time_pause(true)` again. Exactly one frame of scripts runs, with that frame's real `dt`.
- **stop()** (from `Playing` or `Paused`): `stop_scripts()` first (so `on_destroy` runs while the play scene is still active), then
  `time_stop()`, then `show_scene(editScene, false)` and `restore_document(editScene, document)`; the selection is restored by guid; the
  clone is released (the SceneModule takes it down over the next frames).
- **Scene replaced during play** (`update()` sees `active_scene()` differ from the play scene, e.g. a script called `load_scene`): the
  play scene becomes the new active scene and `start_scripts()` runs on it. Stop still returns to the original edit scene.
- **Script errors** never stop play (they are logged and disable that instance, as in sub-project 1).
- **Closing the editor while playing** performs `stop()` first.
- Play refused (no state change) when `can_play()` is false or the state is not `Edit`; `pause`/`resume`/`step`/`stop` outside their
  states do nothing.

## `clone_scene` details

Parents before children, siblings in order, so `Hierarchy` and `Scene::roots()` match. Per entity: `Scene::create(guid, name, parent)`
(makes Id, Name, Transform, Hierarchy, WorldTransform), then every reflected component the source has is added and its fields copied
with the ComponentInfo `Get`/`Set`; non-reflected components (`ValueBags`, `PrefabInstance`, `PrefabChild`, `UnknownComponents`) are
copied as values. Settings: `Scene::set_settings(source.settings())`. Finally `update_world_transforms()`.

## Testing (`--script-smoke`)

- `clone_scene`: a scene with a hierarchy of three levels, several roots, a prefab instance with an override and a generated child
  (`PrefabChild`), a script component with a value bag, an unknown component, non-default settings. The clone has equal guids, parent
  links, sibling and root order, component values, bag values, settings; mutating the clone (change a field, add and destroy entities)
  leaves the source equal to a second clone taken before.
- `PlayMode` on a fake host: Play from Edit; Play refused when `can_play()` is false or already playing; Pause/Resume; Step runs exactly
  two `time_pause` toggles over two updates and only from Paused; Stop order (`stop_scripts`, `time_stop`, `show_scene(edit)`,
  `restore_document`) and that the edit scene object is the one restored; Stop from Paused; scene replaced mid-play restarts scripts and
  Stop still restores the original; `pause`/`step`/`stop` in `Edit` do nothing.
- `Time`: after `init()` the game clock is playing with `TimeScale` 1; after `play_game()` the first `update()` gives a delta under one
  second; `pause_game()` stops the clock advancing.
- `SceneEditor`: while `set_playing(true)`, `save`, `save_as`, `open`, `new_scene` return without changing the document; `restore_document`
  restores path and dirty flag.
- `ScriptingModule`: covered by a `ScriptInstances`-level check that no `on_update` runs when the module's gate is closed; the gate itself
  is a one-line condition on the game clock.

## Out of scope

Hot reload (sub-project 3). Keyboard shortcuts for the controls. A game view through the scene's primary camera. Time-scale slider.
Persisting editor changes made during play.
