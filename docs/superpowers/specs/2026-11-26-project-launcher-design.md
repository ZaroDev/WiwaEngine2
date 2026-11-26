# Project launcher

## Intent

Start real games outside the engine repository. Today `--project <dir>` already makes any folder a project (working directory, `engine/` junction, project-settings.md), but nothing creates one, remembers it or opens it without typing a command line. The launcher is the way in: pick a recent project, create one, or open a folder; it starts the editor on it.

## Goals

- A separate executable `wiwa_launcher` that lists recent projects and creates or opens projects anywhere on disk.
- A new project opens ready to work: a starter scene (camera, directional light, ground plane) set as the default scene.
- The editor can go back to the launcher (File > Switch Project...).

## Non-goals

- Switching projects inside a running editor (the working directory is fixed before the engine exists; switching is a relaunch).
- Templates other than Minimal, project upgrade/migration, deleting projects from disk, version control setup beyond ignore files.
- Building games from the launcher.

## Anti-goals

- The launcher never deletes or moves a user's files. "Remove from list" only edits the recent list.
- No editor-vs-game build macro, no editor code in the game.
- The launcher must not need the engine's renderer, shaders or an asset scan to open: it shows in well under a second.

## Approaches considered

- **A. File menu only** (New/Open Project relaunch the editor). Cheapest, but the editor with no arguments still opens the demo.
- **B. Startup hub inside the editor** (a light mode of `Editor` when no `--project`). One executable, but needs a startup mode that skips most of `Engine::init`.
- **C. Separate launcher executable** (chosen by the user). Clean split, no special mode in the engine; costs a premake project and its own small window/ImGui setup.

## Design

### Project `wiwa_launcher` (`launcher/`)

- Premake project next to the others (`launcher/premake5.lua`, included from the root). ConsoleApp in Debug/Release, WindowedApp in Shipping, like the game. Links `wiwa` (for `core/project_dir.*` and `core/project_settings.*`), `ImGui`, `GLFW`, `d3d11`, `dxgi`. Debugdir `launcher/`. Post-build copies the exe to `launcher\wiwa_launcher.exe`.
- Window: GLFW + ImGui's stock `imgui_impl_glfw` and `imgui_impl_dx11` backends (from `engine/vendor/imgui/backends`, compiled into the launcher, not the ImGui lib). Not `Engine`, not `RenderModule`. Font: CascadiaCode from the engine assets folder (`find_engine_assets`), ImGui default font when not found.
- `launcher/src/main.cpp` (window, loop), `launcher_ui.*` (draws only), `project_registry.*` and `project_creator.*` (ImGui-free logic).

### Recent projects (`ProjectRegistry`)

- `%APPDATA%\Wiwa\projects.json`: list of `{ path, name, lastOpened }` (path absolute, lastOpened ISO-8601 UTC). Missing file = empty list; unreadable = empty list and one warning; written atomically (temp + rename).
- Name shown = `Name` from the folder's `project.wproject`, else the folder name. A missing folder shows greyed with "Locate..." and "Remove from list".
- Sorted most recent first. Opening or creating a project moves it to the top. The same folder (compared after `weakly_canonical`) appears once.

### Add an existing project

For projects the launcher has never seen (made by hand, with `--project`, cloned from git, or on another machine), there's an **Add Existing...** button next to New Project. It opens a folder picker and adds the folder to the recent list *without* opening it. The list is then sorted by last opened, and a project that has never been opened goes at the top until something newer arrives.

- A folder counts as a project when it holds `project.wproject` or an `assets/` folder (the engine's own `editor/` qualifies, so the demo can be added).
- Anything else is refused with "Not a Wiwa project: no project.wproject or assets/ folder in <dir>", and nothing changes.
- A folder that is already in the list is not added twice. It is selected and its row flashes.
- Dropping a folder on the launcher window (GLFW drop callback) does the same as Add Existing for each folder dropped.

**Open Folder...** stays separate: it adds the folder the same way and opens it at once.

### Create project (`ProjectCreator`)

Dialog fields: name, parent folder (folder picker, defaults to `%USERPROFILE%\Documents\Wiwa Projects`), resulting path shown. Refused with a message when: the name is empty or has characters invalid in a Windows folder name, or the target folder exists and is not empty.

Writes:

- `<dir>/project.wproject` with `Name` = the name and every other field default (written through `project_settings` so the format stays the engine's).
- `<dir>/assets/models/shapes/shapes.glb` copied from the committed template (`launcher/templates/minimal/`), so the editor's 3D templates work.
- `<dir>/.gitignore` (`library/`, `engine/`, `imgui.ini`, `*.log`, build output) and `<dir>/.gitattributes` (binary assets as binary).
- A marker `<dir>/.wiwa_new_project` telling the editor to build the starter scene on first open.

Then registers the project and launches it.

### Starter scene (editor side)

Scene files reference assets by GUID, which only exist after the editor imports `shapes.glb`, so the scene is built by the editor, not written by the launcher. On startup, when `.wiwa_new_project` exists, `EditorModule` waits for the startup import scan, then with `entity_templates`: Plane (scaled 10x), Directional light (rotated down), Camera (raised, looking at the origin); saves `assets/scenes/main.wscene`; makes it the build settings' default scene and only scene; deletes the marker. A failure logs once and leaves the marker so the next start retries.

### Launching

- The editor is found next to the launcher's build output: `<bin>/<cfg>/wiwa_launcher/../wiwa_editor/wiwa_editor.exe`, then `wiwa_editor.exe` in the launcher's own folder. Not found: an error in the window, nothing else happens.
- Started with `CreateProcessW` and `--project "<dir>"`, working directory `<dir>`. The launcher closes once the process started.
- File > Switch Project... in the editor: the usual unsaved-scene check, then starts `wiwa_launcher.exe` (found the same way, mirrored) and quits the editor. Disabled while playing.

## Verification

- `--launcher-smoke` in `wiwa_tests` (compiles `launcher/src` minus `main.cpp`/`launcher_ui.*`): registry round trip, dedupe, ordering, missing file, corrupt file; adding an existing folder (accepted with `project.wproject`, accepted with only `assets/`, refused otherwise, no duplicate); creator refusals, files written, `project.wproject` reads back with the name; editor lookup paths. Seen to fail under a negative control.
- The starter scene step runs in a new `--new-project-smoke` against a scratch project folder (create with `ProjectCreator`, run the editor step, check `main.wscene` has the three entities and is the default scene).
- Manual: launcher opens, create a project in a folder outside the repo, editor opens on it with the starter scene, Switch Project returns.
- Demo scene: not applicable (the launcher is not a scene feature); recorded here as the exception to the CLAUDE.md rule.

## Builder's calls (not discussed, change freely)

- Default parent folder `Documents\Wiwa Projects`; recent list in `%APPDATA%\Wiwa`.
- Template folder in `launcher/templates/minimal/`, its `shapes.glb` produced once by `scripts/gen_demo_scene.py`'s `build_glb` (committed binary).
- Folder picker: `IFileOpenDialog` with `FOS_PICKFOLDERS`.
- Docs: new `docs/superpowers/documentation/launcher.md`, CLAUDE.md table and flag rows, project-settings.md pointer.
