# Build settings: default scene and pak contents

## Goal

The editor decides which scenes (and the assets they need) go into `assets.wpak`, and which scene is the default. A packaged game started with no arguments loads the default scene from the pak.

## Current state

- `Packager::pack` (`editor/src/asset_pipeline/packager.cpp`) packs every `.wasset` in `Library/` plus the catalog. No selection.
- `SceneModule::init` loads a scene only when `--scene <path>` is passed.
- Cooked `.wasset` headers record `Dependencies` (guids), so a dependency walk needs no new import data.

## Design

### 1. Build settings file

`editor/build_settings.json`, next to `render_quality.json`. It is not an asset.

```json
{ "defaultScene": "<guid>", "scenes": ["<guid>"], "alwaysInclude": ["<guid>"] }
```

- GUIDs, so renames and moves do not break it.
- File missing: the packer behaves as today (pack everything, no default scene). Existing smoke tests and `--pack` keep working.
- File present: `defaultScene` must be in `scenes`, otherwise packing fails with a clear error.

### 2. Packer

Roots: `scenes` + `alwaysInclude` + every asset under the engine root (engine assets are runtime requirements).

- Walk `Dependencies` transitively through the cooked headers; pack only what is reached.
- Not reached means not in the pak. There is no per-asset "never include" flag (it would contradict the dependency walk).
- The catalog written into the pak is filtered to packed assets, so a path to an excluded scene fails to resolve in the game.
- Log counts: scenes, packed assets, excluded assets.

### 3. Default scene in the pak

A reserved-GUID entry (like `CATALOG_GUID`) holds a small build-info blob containing the default scene's guid. No pak format version change.

### 4. Runtime

In `SceneModule::init`: if there is no `--scene` argument and the `AssetManager` source is a `PakSource` containing build info, call `load_async` on the default scene and store it in `m_StartupLoad`. `--scene` still overrides. `--scene-smoke-load` and the renderer's startup wait are unchanged.

### 5. Editor UI

A "Build Settings" panel:

- Lists every `.wscene` with an "include" checkbox and a "default" radio button.
- An "Always include" asset list, fed by an asset-browser context-menu item "Always include in build".
- A "Pack" button that calls `pack_async` to `../game/`.

### 6. Tests

Extend the asset smoke test:

- An excluded scene and its exclusive assets are absent from the pak.
- An asset shared with an included scene is present.
- An `alwaysInclude` asset with no referrer is present.
- The default scene guid round-trips through build info.
- With no settings file, everything is packed.

Also run `wiwa_game --scene-smoke-load` with no `--scene` against a built pak.

## Out of scope

Multiple build profiles, platform-specific lists.
