# Settings page and project settings

## Goal

One place in the editor to edit every project-wide setting (renderer, physics, input, performance, launch), and a project file that names the project and tells the game how to launch.

## Decisions

- **One project, no project switching.** `project.wproject` lives in the editor working directory like the other settings files. No New/Open Project, no recent list.
- **Settings page owns project-wide values** (option C). Render quality leaves the World panel, physics layers leave the Physics panel, the default input map leaves the Input panel. Per-scene values (gravity, fixed rate, ...) stay in the scene and the Physics panel.
- **Build Settings stays a popup** and owns scenes and the default scene. The project file does not hold a default scene.
- **The panel is closed by default** (like the other optional panels; opened from the Window menu).
- **No new smoke checks** (user decision: they test the page by hand). The demo scene still shows the Lua `project` API.

## Project file

`ProjectSettings` (engine, plain data, `engine/src/core/project_settings.{h,cpp}`) with `load`/`save`/`sanitized`, same shape as `RenderQuality`. JSON, missing keys keep defaults, unknown keys ignored, values sanitized.

| Group | Fields |
| --- | --- |
| Identity | name, company, version (major.minor.patch + optional label), description |
| Window | title (defaults to name), width, height, mode (windowed / borderless / fullscreen), resizable |
| Launch | graphics API (vulkan / d3d11 / d3d12), debug draw on start, GPU validation |
| Performance | FPS cap (0 = uncapped), render scale, fixed-timestep rate |

Vsync and the copy queue stay in `RenderQuality` and are shown under Renderer.

Storage and loading:
- Editor: `project.wproject` in the working directory. Missing or unreadable gives defaults, logged once.
- Pak: the packager writes it under a new reserved GUID (`PROJECT_SETTINGS_GUID`) beside `PHYSICS_SETTINGS_GUID` and `INPUT_SETTINGS_GUID`. `RenderQuality` is unchanged: it stays in `render_quality.json`, read from next to the program, and is not packed.
- Game precedence: CLI flag, then pak value, then default.
- The editor ignores window size and mode (it owns its window). It uses name and version for the title bar and the Export output.

## Settings page

`SettingsPanel` (`editor/src/panels/settings_panel.{h,cpp}`), dockable, `m_Open = false`. Category list on the left, editor on the right, a search box filtering rows across categories.

| Category | Content | Moved from |
| --- | --- | --- |
| Project | identity | new |
| Launch | window, API, startup flags | new |
| Performance | FPS cap, render scale, fixed rate | new |
| Renderer | all of `RenderQuality` | World panel, Quality section |
| Physics | layer names, collision matrix | Physics panel, Layers tab |
| Input | default `.winput` map | Input panel, default-map row |
| Editor | gizmo toggles (`gizmo_settings.json`) | Viewport Gizmos menu (kept there too) |

Structure: each category is an ImGui-free `*SettingsSection` (`editor/src/settings/`) with `load()`, `save()`, `apply()`, `last_error()`. An edit saves its file at once and applies to the running module. The panel only draws rows. Rows show a "restart needed" tag for launch-only values (API, window mode, GPU validation) and a reset-to-default button.

Leftover panels:
- Physics panel keeps the scene's step settings and Stats, plus a link to Settings.
- Input panel keeps the map editor and live values, plus a link.
- World panel loses the Quality section, plus a link.

## Engine changes

- `Window` reads title, size, mode, resizable.
- `RenderModule` reads render scale and the API default (the `--api=` flag still wins, no silent fallback).
- `Engine` applies the FPS cap and the fixed-timestep rate.
- Game startup applies debug draw and GPU validation defaults.
- Pak loading resolves the new reserved GUID.

## Lua API

A read-only `project` table (main thread, available in scripts and in the LuaLS stubs):
- `project.name`, `project.company`, `project.version` (string `"1.2.3"` or `"1.2.3-label"`), `project.description`.
- `project.version_major`, `project.version_minor`, `project.version_patch`.

Documented in `docs/scripting.md` and the scripting spec. `demo.wscene` gets a HUD readout of name and version.

## Docs to update in the same change

New `project-settings.md` (this design, as it ends up). Update `assets.md` (project files table, reserved GUIDs), `editor.md` (panel list), `rendering.md`, `physics.md`, `input.md` (where their settings are edited), `scripting.md` and `docs/scripting.md` (the `project` table), `README.md` index. The Renderer category keeps editing `render_quality.json` as today.

## Verification

No new smoke flags. Existing `--asset-smoke`, `--scene-smoke`, `--script-smoke`, `--demo-smoke` must still pass (the pak and script surface change). The user tests the page by hand.

## Risks

- An old pak without the project entry falls back to defaults.
- Existing `--render-test` checks use `RenderQuality` directly and must be unaffected.
- The panels that lose rows are edited in place; keep their public APIs where other code (smokes) calls them.
