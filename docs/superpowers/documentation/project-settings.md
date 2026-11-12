# Project settings and the Settings page

The project-wide values a game starts from, and the one editor panel that edits every project-wide setting.

## The file

`project.wproject` (JSON, `PROJECT_SETTINGS_FILE`) in the editor's working directory; the code is `engine/src/core/project_settings.{h,cpp}`. The packager copies it into `assets.wpak` as a blob under `PROJECT_SETTINGS_GUID` (not an asset: no real guid equals it). Fields: name, company, description, version (major/minor/patch/label), window (title, size, mode, resizable), launch (API, debug draw on start, GPU validation), performance (FPS cap, fixed rate).

- Load: a missing key keeps its default, an unknown key is ignored, a bad value is sanitized (`sanitized()`), an unknown `mode`/`api` string keeps the default and logs a warning. A missing file gives the defaults silently; an unreadable one gives the defaults and one warning. The editor creates the file with the defaults on first run.
- Pak blob: version-prefixed (`PROJECT_SETTINGS_VERSION`); a blob of another version, or one that does not read to the end, is refused and the engine falls back to the defaults. A pack made before this existed has no blob, which is the same as the defaults.
- `Engine::init` loads it right after `AssetManager` (from the pak when it holds the blob, else from the file) into `Engine::project()`; `project_from_pak()` tells which.

## Who uses which value

| Value | Used by | Notes |
| --- | --- | --- |
| window title | `Window` | `window_title()`: the title, or the name when empty. Both empty keeps `Wiwa 2 <config>` |
| window size, mode, resizable | `Window` | only when `project_from_pak()` (a game). The editor owns its window: always 1280x720 windowed |
| API, GPU validation | `RenderModule::read_arguments` | only from the pak; `--api=` and `--gpu-validation` still win |
| debug draw on start | the game's startup | `--debug-draw` still switches it on |
| FPS cap | `Engine::init` (`Time::set_target_fps`) | 0 = uncapped |
| fixed rate | `SceneEditor::new_scene` | the physics rate a *new* scene starts with; a scene keeps its own value. 0 = built-in default |
| name, company, description, version | Lua `project` table (read-only), editor window title | see `docs/scripting.md` |

Vsync and the copy queue are not here: they stay in `RenderQuality` (`render_quality.json`, unchanged, rendering.md).

## The Settings page

`SettingsPanel` (`editor/src/panels/settings_panel.*`, closed until opened from the Window menu): pages on the left (Project, Launch, Performance, Renderer, Physics, Input, Audio, Editor; the Renderer page has groups for shadows, post, anti-aliasing and Particles, vfx.md), rows on the right, a search box that filters rows across all pages. Each row has a reset-to-default button once it differs. Launch values are tagged "(restart needed)".

The panel only draws. Each page is backed by an ImGui-free `ISettingsSection` (`editor/src/settings/`): it owns the value and its file, saves at once on a change and applies it to the module that uses it.

| Section | Value and file | Applied to |
| --- | --- | --- |
| `ProjectSection` | `project.wproject` (Project, Launch, Performance pages) | `Engine::set_project`, window title |
| `RendererSection` | `render_quality.json`, through `RenderModule` | the render thread |
| `PhysicsSection` | `physics.wsettings` layers and matrix | `PhysicsModule::set_layers` |
| `InputSection` | `input.wsettings` default map | `InputModule::set_default_map` |
| `AudioSection` | `audio.wsettings`: backend (Null, MiniAudio, Wwise), soundbanks, memory pool, language | `AudioModule::select_backend` and the bank list; the editor applies it once the first import scan has cooked the banks (audio.md) |
| `NavSection` | `nav.wsettings` named areas and costs (below); it has no page: the Navigation panel edits it | `NavModule` |
| `EditorSection` | `gizmo_settings.json` | read by the Viewport every frame |

Traps:

- The Physics and Input panels and the Viewport do not own these values any more; they share the section with the Settings page, so only one object writes each file. Keep `PhysicsPanel`/`InputPanel`'s forwarding methods: the smokes call them.
- `EditorSection` saves when the mouse is let go (`flush`), so a slider drag writes once. Call it every frame from every widget that edits it.
- The World panel keeps a one-line hint to Settings where the Quality section used to be.

## Navigation settings

`nav.wsettings` (editor working directory, JSON `areaCost`: area name to cost, each at least 1) is the project's walking costs. It is edited in the Navigation panel (`NavSection`, saved at once, applied to `NavModule`), packed with the game, and read by `NavModule::init` there. See [navigation.md](navigation.md).
