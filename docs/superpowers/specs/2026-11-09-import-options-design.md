# Import options and import dialog

Status: design approved 2026-11-09, not implemented. Not committed (spec/plan docs of this kind are not committed).

## Goal

Every importer declares typed import options with defaults. Importers marked editable expose them in the editor through an import dialog, and editable assets can be reimported with one action. Importers that are not editable keep their defaults and the pipeline stays uniform, so adding an asset type never needs special cases.

Success: in the editor, right-click a texture, model or font > "Import Settings...", change options, Apply, and the asset is cooked again with them; "Reimport" recooks any importable asset; files dropped onto the Asset Browser from the OS are copied in, imported with defaults, then offered the dialog if editable.

## Current state (verified)

- `<file>.meta` already holds `importSettings` (`MetaFile::Settings`); `IImporter::default_settings()` exists; `ImportContext::settings()` reads them. The settings hash feeds `Library/import_cache.json`.
- Existing options: texture `kind`; model `generatePhysicsShapes`; font `pixelSize`, `sdfRange`, `charset`; shader `debugInfo`. Models also keep importer-owned state in `Settings` (`extracted`, `materialFiles`): the editor UI must never overwrite those keys.
- `AssetDatabase::reimport(path, force)` exists. No UI edits settings; the only editor reimport is "Re-extract from model..." in the Asset Browser.
- There is no OS file-drop support in `Window`.

## Design

### 1. Pipeline (`editor/src/asset_pipeline`)

- `ImportOption`: key, label, tooltip, type (bool, int, float, enum, string), default, min/max or choice list.
- `IImporter` gains `virtual std::vector<ImportOption> option_schema() const` (empty by default) and `virtual bool editable() const` (false by default). `default_settings()` is derived from the schema when one exists (single source of truth); importers without a schema keep their hand-written defaults.
- Editable: textures, models, fonts. Not editable: scenes, materials, scripts, shaders, and the rest. Shader `debugInfo` moves to a schema but stays non-editable.
- Option lists (only options the importers honour):
  - Texture: `kind`, `generateMipmaps`, `maxSize` (0 = no limit; larger sources lose top mips).
  - Model: `generatePhysicsShapes`.
  - Font: `pixelSize`, `sdfRange`, `charset` (ranges as in the current importer validation).
  - Not offered because no importer honours them yet: texture `wrap`, model `scale`, `mergeMeshes`, `calculateTangents`.
- Importer `version()` is bumped only where the cooked output changes.
- `AssetDatabase::set_import_settings(source, values)`: refuses non-editable importers; validates and clamps against the schema; merges only schema keys into `Settings` (importer-owned keys are untouched); saves the `.meta`; calls `reimport(source, true)`. Unchanged settings do not recook (the settings hash already decides).
- `AssetDatabase::reset_import_settings(source)`: schema defaults, then reimport.
- A failed import keeps the previous cooked asset and marks the asset Failed, as today; the error is reported back to the caller.

### 2. Import dialog (model without ImGui, plus panel UI)

- `ImportSettingsModel` (no ImGui): loads the `.meta` settings, holds edited values, diff against saved and against defaults, validation, apply. This is where the logic lives.
- `ImportSettingsDialog` (ImGui modal in `editor/src/panels`): asset name and importer, one widget per option generated from the schema, "differs from default" marker, buttons Apply (disabled when nothing changed), Revert, Defaults, Cancel. A failed apply shows the error in the dialog.

### 3. Reimport shortcuts (`AssetBrowserPanel`)

- Context menu on an editable asset: "Import Settings..." and "Reimport".
- "Reimport" works on any importable asset (forced recook with the current settings).
- "Reimport folder" loops over the importable files in a folder.
- Multi-selection reimport only if the Browser already supports multi-selection (checked in the plan).

### 4. Drag-drop

- `Window` registers a GLFW drop callback and queues the dropped paths for the main thread.
- The editor copies dropped files into the folder open in the Asset Browser (originals untouched), imports them with defaults as the watcher does, then opens the dialog for each editable file in turn. Non-editable files import silently.
- Cancel keeps the default import; nothing is deleted. No pending-import state is needed.
- Files found by the watcher or a scan never open the dialog.

### 5. Verification and docs

- No new smoke checks and no smokes run, by the user's decision. Validation lives in `ImportSettingsModel` so tests can be added later.
- Verify by a Release build.
- Update `assets.md` (schema, editable flag, settings API) and `editor.md` (dialog, Browser menu, drag-drop) in the same change.
- Demo scene: unchanged. This is editor tooling with no runtime feature to showcase (exception to the "showcase in demo" rule).

## Out of scope

Dialog on watcher-detected files, presets, batch settings editing across many assets, per-platform overrides.
