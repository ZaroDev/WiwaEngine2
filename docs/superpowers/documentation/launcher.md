# Project launcher

`wiwa_launcher` (`launcher/`) is the way into a game that lives outside the engine repository. It lists projects, adds existing ones, creates new ones, and starts the editor on the one you pick with `--project <dir>` (project-settings.md). It is a separate executable so that it opens at once: it has its own GLFW window with ImGui's stock D3D11 backend (`engine/vendor/imgui/backends`, compiled into the launcher), and it never starts `Engine`, the renderer or an asset scan. It links the engine only for `core/project_dir` and `core/project_settings`.

## Files

| File | What |
| --- | --- |
| `launcher/src/core/project_registry.*` | The recent list (`ProjectRegistry`), `is_project_folder`, `project_display_name`. ImGui-free |
| `launcher/src/core/project_creator.*` | `create_project`, `valid_project_name`, `find_template`, `default_projects_folder`. ImGui-free |
| `launcher/src/core/launch.*` | `find_sibling_executable`, `editor_command_line`, `start_process`. Also compiled into the editor (Switch Project) |
| `launcher/src/launcher_ui.*`, `folder_dialog.*`, `main.cpp` | The window. It only draws and calls the core |
| `launcher/templates/minimal/` | What a new project is copied from: `assets/models/shapes/shapes.glb`, written by `scripts/write_template_shapes.py` (`build_glb` of `gen_demo_scene.py`) |
| `editor/src/project/starter_scene.*` | The editor's half: the starter scene on a new project's first open |

`wiwa_tests` compiles `launcher/src/core` and checks it with `--launcher-smoke`. `--new-project-smoke` creates a project with the real template and runs a child `wiwa_tests --project <scratch> --starter-scene-child` (a real editor start-up) to check the starter scene. The child runs in a child process because the tests' own process works in `editor/`, and the step would write that project's scenes and build settings.

## The recent list

`%APPDATA%\Wiwa\projects.json`: `{"projects": [{"path", "name", "added", "lastOpened"?}]}`. Paths are UTF-8 with forward slashes, and times are ISO-8601 UTC to the second. A missing file is an empty list. An unreadable one is an empty list plus one warning, and is overwritten on the next save. Unknown keys are ignored. The list is saved through a temporary file and a rename.

- **A project** is a folder holding `project.wproject` or `assets/`. The engine's own `editor/` qualifies. Anything else is refused with exactly `Not a Wiwa project: no project.wproject or assets/ folder in <dir>`.
- **The same folder written two ways** is one entry: paths are compared after `weakly_canonical` and case-insensitively.
- **Sorting** is newest first, by `lastOpened`, or `added` for a project the launcher has never opened.
- **Remove from list** and **Locate...** only edit the list. The launcher never deletes or moves a user's files.
- **Adding:** Add Existing... and dropping folders on the window add without opening. Open Folder... adds and opens. Adding a listed folder selects its row and flashes it for a second.

The launcher's own log (`wiwa.log`) is also in `%APPDATA%\Wiwa`: the launcher makes that folder its working directory before `Log::init`, which writes to the working directory.

## New projects

The dialog asks for a name and a location (default `Documents\Wiwa Projects`). The name must be a valid Windows folder name (`valid_project_name`).

`create_project` refuses a folder that exists and is not empty, and touches nothing in it. Otherwise it copies the template and writes:

- `project.wproject`, with the name and the defaults
- `.gitignore`: `library/`, `engine/`, `imgui.ini`, `*.log`, `bin/`, `assets.wpak`
- `.gitattributes`: binary asset types
- the marker `.wiwa_new_project` (`NEW_PROJECT_MARKER`, `core/project_dir.h`)

The template is found from `WIWA_LAUNCHER_TEMPLATES`, then by walking up from the executable to `launcher/templates/<name>`.

### The starter scene (editor)

The launcher cannot write the scene: a scene names assets by guid, and `shapes.glb` only gets one when the editor imports it. `EditorModule::update` calls `build_starter_scene` every frame until it settles:

1. **No marker:** `NotNeeded`.
2. **First scan still running:** `NotYet`.
3. **Shapes model not in the catalog yet:** `NotYet`. `find_asset` re-reads the catalog at most every 250 ms.
4. **Otherwise:** a new scene with Plane (scale 10, 1, 10), Directional light and Camera (0, 4, 8, looking down at the origin) from `entity_templates`. It is saved as `assets/scenes/main.wscene` and imported at once.
5. **Once the scene has a guid:** `build_settings.json` gets it as the default and only scene, the marker is deleted, and the step returns `Done`.

A step that still says `NotYet` 15 s after the scan (`STARTER_SCENE_WAIT_SECONDS`) or fails is logged once and keeps the marker, so the next start tries again. A start that saved the scene but failed before the build settings only finishes the build settings next time.

## Launching

- **Finding the editor:** `find_sibling_executable` tries `<launcher folder>/../wiwa_editor/wiwa_editor.exe` (the build output, `bin/<cfg>/`), then `wiwa_editor.exe` next to the launcher.
- **Starting it:** `CreateProcessW` with `"<editor>" --project "<dir>"`, working directory `<dir>`. The launcher closes once the editor has started, and stays open with the error otherwise.
- **File > Switch Project...** in the editor (not while playing) finds `wiwa_launcher` the same way and starts it, then quits the editor. With unsaved changes it first asks: Save and switch (only for a scene that has a path), Switch without saving, or Cancel.

## Traps

- The build has no `/utf-8` flag: non-ASCII string literals in sources must be `\u` escapes, or they are read in the system code page.
- `AssetCatalog::find` compares paths case-insensitively (ASCII). The catalog stores the source root as its folder is named (`assets/...`), while code looks up `Assets/...`. Before this, the Hierarchy's 3D shape templates never appeared.
- The launcher is not a scene feature, so it is not in `demo.wscene` (the one exception to that CLAUDE.md rule, decided in the design).
