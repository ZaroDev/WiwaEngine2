# Game UI

Code: `engine/src/ui`, render pass `renderer/passes/ui_pass.*` + `editor/engine/shaders/ui.hlsl`, Lua `scripting/bindings/bind_ui.cpp` + `scripting/ui_handlers.*`, importers `font_importer.cpp` / `ui_panel_importer.cpp`, editor `editor/src/ui`. Verified by `--ui-smoke` and `--ui-editor-smoke`. Lua API: `docs/scripting.md` (UI section).

## Data

A `UICanvas` component (`Panel` AssetRef, `SortOrder`, `Visible`) shows a `.wuipanel` asset: a **flat** widget tree stored by `UiId` and `Parent` (`UiPanelData`, `ui_widget.h`). Kinds: Panel, HStack, VStack, Text, Image, Button, Slider, Toggle, Bar, ViewImage (the picture of an offscreen view named by `ViewName`, views.md; the UI editor canvas does not draw it). `UiWidget` is one reflected struct for every kind; fields a kind does not use keep defaults (`ui_field_visible`). **Fields are described once by `ui_fields()`**, which the panel files, the UI editor's inspector and the Lua bindings all read - add a field there, not in three places. Ids are stable inside a panel and never reused by the editor; widget names are unique among siblings and scripts find widgets by name path. A panel with more than `UI_MAX_WIDGETS` is rejected as corrupt.

Fonts (`.ttf`/`.otf`) import to a `Font` asset: an SDF atlas built with `stb_truetype` and glyph metrics. Text layout (`ui_font.*`): wrap, align, missing glyphs fall back, invalid UTF-8 and very long words do not loop.

## Per-frame flow

`UiModule` (priority -10: after scene and renderer, before the scripts) keeps one `UiPanelInstance` (a mutable copy of the panel) per canvas.

1. `update()` feeds the input frame to the interaction state machine (the topmost canvas first): hover, press, focus, keyboard/gamepad navigation for Button, Slider, Toggle; events (`Click`, `ValueChanged`, `HoverEnter`, `HoverExit`, `Focus`) are kept on the instance. This runs before the scripts, so a script sees this frame's clicks.
2. `ScriptInstances::update` dispatches the events to Lua handlers registered through `UiHandlerTable` (a handler belongs to the script instance's `self` and disappears with it).
3. `pre_render_update()` (after scripts moved things): layout, build a flat `UiDrawList`, hand it to `RenderModule`.

## Layout

Rect resolution is anchors (fractions of the parent) plus pixel offsets at a reference resolution, scaled to the canvas; HStack/VStack lay children out with spacing, direction and child alignment. A widget can be **anchored to an entity in 3D space**: its point is projected through the primary camera (or `set_camera_override`, the editor's fly camera) as a **screen-space billboard** - never depth tested, sorted far to near inside a canvas. A missing/destroyed entity or a point behind the camera hides the widget (`UiOffscreen::Hide`) or clamps it to the edge. Zero-size canvases give an empty draw list; degenerate values (slider `min == max`, `step <= 0`, bar value outside 0..1, anchor min > max) are clamped, never NaN. A panel that is Loading or Failed draws nothing and logs once.

## Rendering

`UiRenderPass` (render thread) draws the draw list into the LDR image after FXAA (so post never touches the UI) with SDF text from `ui.hlsl`; it also draws the UI editor's canvas into its own texture (`set_ui_preview`). Font atlases are uploaded by `GpuResourceCache`. No depth-tested/3D canvases, no MSDF.

## Input

An `IUiInputSource` supplies the `UiInputFrame` (pointer, down/pressed/released, scroll, navigation presses). `NullInputSource` is no input; `ScriptedInputSource` (`UiModule::scripted_input()`, `UI.input` in Lua) is filled by code; in the game and in the editor's Play the source is `InputModule`'s `InputUiSource` (input.md). `EditorPlayInputSource` (ImGui-fed) is used only by the UI smoke check. Not supported: text input fields, scroll views, animation, localization, rich text.

## Editor

`UiEditModel` is the ImGui-free document with undo (tested by `--ui-smoke`); `UiEditorPanel` is the window: widget tree, inspector (from `ui_fields()`), a canvas preview rendered by the render thread into its own texture. `--ui-editor-smoke` opens a panel and previews it for 130 frames with ImGui asserts on. The demo scene has a HUD panel, script and `--demo-smoke` coverage.
