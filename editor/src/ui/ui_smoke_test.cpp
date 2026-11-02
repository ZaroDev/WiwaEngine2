#include <editor_common.h>
#include "ui_smoke_test.h"

#include "../asset_pipeline/asset_database.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <assets/asset_manager.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <glm/gtc/matrix_transform.hpp>
#include <ui/ui_font.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_layout.h>
#include <ui/ui_panel_io.h>
#include <ui/ui_widget.h>

#define WI_UI_LOG(level, ...) WI_LOG(level, "UI", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_UI_LOG(Error, "UI smoke test FAILED: {0}", what);
				}
			}
		};

		UiWidget make_widget(UiId id, UiId parent, WidgetKind kind, const char* name)
		{
			UiWidget widget;
			widget.Id = id;
			widget.Parent = parent;
			widget.Kind = kind;
			widget.Name = name;
			return widget;
		}

		// One widget of every kind, nested, with a world anchor and values that differ from the defaults.
		UiPanelData make_sample_panel()
		{
			UiPanelData panel;
			panel.ReferenceResolution = { 1280.0f, 720.0f };
			panel.MatchWidthOrHeight = 0.25f;

			UiWidget menu = make_widget(1, 0, WidgetKind::VStack, "Menu");
			menu.Spacing = 8.0f;
			menu.Padding = { 4.0f, 5.0f, 6.0f, 7.0f };
			menu.WorldEntity = EntityGuid::generate();
			menu.WorldOffset = { 0.0f, 1.5f, 0.0f };
			menu.WorldScaleWithDistance = true;
			menu.WorldOffscreen = UiOffscreen::ClampToEdge;
			panel.Widgets.push_back(menu);

			UiWidget title = make_widget(2, 1, WidgetKind::Text, "Title");
			title.Text = "H\xC3\xA9llo \xC3\xB1 \"quoted\"\n";
			title.FontSize = 32.0f;
			title.Font = AssetRef{ AssetGuid::generate(), 0 };
			title.Align = UiTextAlign::Center;
			title.Color = { 0.1f, 0.2f, 0.3f, 0.4f };
			panel.Widgets.push_back(title);

			UiWidget button = make_widget(3, 1, WidgetKind::Button, "Start");
			button.HoverColor = { 0.1f, 0.2f, 0.3f, 1.0f };
			button.Enabled = false;
			panel.Widgets.push_back(button);

			UiWidget slider = make_widget(4, 1, WidgetKind::Slider, "Volume");
			slider.Value = 0.7f;
			slider.MaxValue = 20.0f;
			slider.Step = 0.5f;
			panel.Widgets.push_back(slider);

			UiWidget bar = make_widget(5, 1, WidgetKind::Bar, "Health");
			bar.Value = 0.25f;
			bar.Direction = UiDirection::TopToBottom;
			panel.Widgets.push_back(bar);

			UiWidget image = make_widget(6, 0, WidgetKind::Image, "Logo");
			image.Texture = AssetRef{ AssetGuid::generate(), 5 };
			image.Slice9 = true;
			image.OffsetMax = { 64.0f, 64.0f };
			panel.Widgets.push_back(image);

			UiWidget toggle = make_widget(7, 0, WidgetKind::Toggle, "Mute");
			toggle.Value = 1.0f;
			panel.Widgets.push_back(toggle);

			UiWidget background = make_widget(8, 0, WidgetKind::Panel, "Background");
			background.ClipChildren = true;
			background.Opacity = 0.5f;
			panel.Widgets.push_back(background);

			panel.NextId = 9;
			return panel;
		}

		bool widgets_equal(const UiWidget& a, const UiWidget& b)
		{
			if (a.Id != b.Id || a.Parent != b.Parent || a.Kind != b.Kind)
			{
				return false;
			}
			for (const UiField& field : ui_fields())
			{
				if (field.Get(a) != field.Get(b))
				{
					return false;
				}
			}
			return true;
		}

		bool panels_equal(const UiPanelData& a, const UiPanelData& b)
		{
			if (a.ReferenceResolution != b.ReferenceResolution || a.MatchWidthOrHeight != b.MatchWidthOrHeight
				|| a.NextId != b.NextId || a.Widgets.size() != b.Widgets.size())
			{
				return false;
			}
			for (size_t i = 0; i < a.Widgets.size(); i++)
			{
				if (!widgets_equal(a.Widgets[i], b.Widgets[i]))
				{
					return false;
				}
			}
			return true;
		}

		void check_panel_files(Check& check)
		{
			const UiPanelData sample = make_sample_panel();

			std::string error;
			const std::optional<UiPanelData> viaJson = ui_panel_from_json(ui_panel_to_json(sample), error);
			check(viaJson.has_value() && error.empty(), "a panel loads from the JSON it was written to (" + error + ")");
			check(viaJson && panels_equal(sample, *viaJson), "panel_json_roundtrip: every field of every widget survives");

			const std::vector<std::byte> blob = serialize(sample);
			const std::optional<UiPanelData> viaBinary = deserialize<UiPanelData>(blob);
			check(viaBinary && panels_equal(sample, *viaBinary), "panel_binary_roundtrip: every field of every widget survives");
			check(!deserialize<UiPanelData>(std::span<const std::byte>(blob.data(), blob.size() / 2)).has_value(), "a truncated cooked panel is rejected");

			UiJson unknown = ui_panel_to_json(sample);
			unknown["futureSetting"] = 1;
			unknown["widgets"][0]["futureField"] = "x";
			const std::optional<UiPanelData> withUnknown = ui_panel_from_json(unknown, error);
			check(withUnknown && panels_equal(sample, *withUnknown), "panel_unknown_field_kept_out: unknown keys are ignored");

			UiJson noKind = ui_panel_to_json(sample);
			noKind["widgets"][1].erase("kind");
			error.clear();
			check(!ui_panel_from_json(noKind, error).has_value() && !error.empty(), "panel_bad_json_reports: a widget without a kind is rejected with a message");

			UiJson badKind = ui_panel_to_json(sample);
			badKind["widgets"][1]["kind"] = "Hologram";
			error.clear();
			check(!ui_panel_from_json(badKind, error).has_value() && !error.empty(), "an unknown widget kind is rejected with a message");

			UiJson badType = ui_panel_to_json(sample);
			badType["widgets"][1]["Opacity"] = "very";
			error.clear();
			check(!ui_panel_from_json(badType, error).has_value() && error.find("Opacity") != std::string::npos, "a value of the wrong type names its field");

			UiJson duplicate = ui_panel_to_json(sample);
			duplicate["widgets"][2]["id"] = 1;
			error.clear();
			check(!ui_panel_from_json(duplicate, error).has_value() && !error.empty(), "ids_unique: duplicate widget ids are rejected");

			UiJson zeroId = ui_panel_to_json(sample);
			zeroId["widgets"][2]["id"] = 0;
			check(!ui_panel_from_json(zeroId, error).has_value(), "widget id 0 is rejected (0 means no widget)");

			UiJson orphan = ui_panel_to_json(sample);
			orphan["widgets"][2]["parent"] = 99;
			check(!ui_panel_from_json(orphan, error).has_value(), "a parent that does not exist is rejected");

			UiJson loop = ui_panel_to_json(sample);
			loop["widgets"][0]["parent"] = 2; // 1 -> 2 -> 1
			check(!ui_panel_from_json(loop, error).has_value(), "a parent loop is rejected");

			UiJson badResolution = ui_panel_to_json(sample);
			badResolution["referenceResolution"] = { 0, 720 };
			check(!ui_panel_from_json(badResolution, error).has_value(), "a reference resolution with a zero side is rejected");

			check(!ui_panel_from_json(UiJson::array(), error).has_value(), "a panel that is not an object is rejected");
			check(ui_panel_from_json(UiJson::object(), error).has_value(), "an empty object is an empty panel");
		}

		UiWidget layout_widget(UiId id, UiId parent, WidgetKind kind, const char* name, glm::vec2 anchorMin, glm::vec2 anchorMax, glm::vec2 offsetMin, glm::vec2 offsetMax)
		{
			UiWidget widget = make_widget(id, parent, kind, name);
			widget.AnchorMin = anchorMin;
			widget.AnchorMax = anchorMax;
			widget.OffsetMin = offsetMin;
			widget.OffsetMax = offsetMax;
			return widget;
		}

		// A camera at (0, 0, 5) looking down -z, 60 degrees, for a 16:9 canvas. NDC y points up.
		UiCameraView test_camera()
		{
			UiCameraView view;
			view.Position = { 0.0f, 0.0f, 5.0f };
			const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			view.ViewProjection = projection * glm::lookAt(view.Position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			view.Valid = true;
			return view;
		}

		bool finite(const UiRect& rect)
		{
			return std::isfinite(rect.Min.x) && std::isfinite(rect.Min.y) && std::isfinite(rect.Max.x) && std::isfinite(rect.Max.y);
		}

		bool near_rect(const UiRect& rect, glm::vec2 min, glm::vec2 max, float tolerance = 0.5f)
		{
			return glm::length(rect.Min - min) <= tolerance && glm::length(rect.Max - max) <= tolerance;
		}

		void check_layout(Check& check, const FontData* font)
		{
			const glm::vec2 canvas{ 1920.0f, 1080.0f };
			const EntityGuid target = EntityGuid::generate();
			glm::vec3 targetPosition{ 0.0f, 0.0f, 0.0f };
			bool targetExists = true;
			const UiCameraView camera = test_camera();

			UiLayoutInput input;
			input.CanvasSize = canvas;
			input.Camera = &camera;
			input.WorldPosition = [&](const EntityGuid& guid) -> std::optional<glm::vec3>
			{
				if (guid == target && targetExists)
				{
					return targetPosition;
				}
				return std::nullopt;
			};
			input.Fonts = [font](const AssetRef&) { return font; };

			const auto run = [&](std::vector<UiWidget> widgets, const glm::vec2& size = glm::vec2(1920.0f, 1080.0f))
			{
				UiPanelData panel;
				panel.Widgets = std::move(widgets);
				UiLayoutInput local = input;
				local.CanvasSize = size;
				return compute_layout(panel, local);
			};

			// anchors and offsets
			UiLayoutResult result = run({ layout_widget(1, 0, WidgetKind::Panel, "Fill", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }) });
			check(result.Rects.size() == 1 && near_rect(result.Rects[0], { 0, 0 }, canvas) && result.Visible[0] == 1 && result.Opacity[0] == 1.0f, "layout_stretch: anchors 0..1 with no offsets fill the canvas");

			result = run({ layout_widget(1, 0, WidgetKind::Panel, "Corner", { 1, 1 }, { 1, 1 }, { -100, -30 }, { 0, 0 }) });
			check(near_rect(result.Rects[0], { 1820, 1050 }, canvas), "layout_corner_anchor: anchored to the bottom right, a 100x30 widget sits in the corner");

			result = run({ layout_widget(1, 0, WidgetKind::Panel, "Inset", { 0, 0 }, { 1, 1 }, { 10, 20 }, { -30, -40 }) });
			check(near_rect(result.Rects[0], { 10, 20 }, { 1890, 1040 }), "layout_stretch: offsets move the edges in from the anchors");

			result = run({ layout_widget(1, 0, WidgetKind::Panel, "Swapped", { 1, 1 }, { 0, 0 }, { 0, 0 }, { 0, 0 }) });
			check(result.Rects[0].Max.x > result.Rects[0].Min.x && result.Rects[0].Max.y > result.Rects[0].Min.y && finite(result.Rects[0]), "layout_anchor_swapped: anchor min above max still gives a rect with an area");

			// nesting
			result = run({ layout_widget(1, 0, WidgetKind::Panel, "Parent", { 0, 0 }, { 0.5f, 0.5f }, { 0, 0 }, { 0, 0 }),
				layout_widget(2, 1, WidgetKind::Panel, "Child", { 0.5f, 0.5f }, { 1, 1 }, { 0, 0 }, { 0, 0 }) });
			check(near_rect(result.Rects[1], { 480, 270 }, { 960, 540 }), "layout_nested: a child's anchors are fractions of its parent's rect");

			// the reference resolution
			result = run({ layout_widget(1, 0, WidgetKind::Panel, "Box", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 }) }, { 3840.0f, 2160.0f });
			check(near_rect(result.Rects[0], { 0, 0 }, { 200, 60 }) && std::abs(result.CanvasScale - 2.0f) < 1e-4f && std::abs(result.Scale[0] - 2.0f) < 1e-4f, "layout_scale_match: a canvas twice the reference size doubles pixel sizes");

			UiPanelData wide;
			wide.Widgets = { layout_widget(1, 0, WidgetKind::Panel, "Box", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 }) };
			wide.MatchWidthOrHeight = 0.0f;
			UiLayoutInput wideInput = input;
			wideInput.CanvasSize = { 3840.0f, 1080.0f };
			check(std::abs(compute_layout(wide, wideInput).CanvasScale - 2.0f) < 1e-4f, "layout_scale_match: match 0 scales with the width");
			wide.MatchWidthOrHeight = 1.0f;
			check(std::abs(compute_layout(wide, wideInput).CanvasScale - 1.0f) < 1e-4f, "layout_scale_match: match 1 scales with the height");

			// visibility and opacity
			UiWidget hiddenParent = layout_widget(1, 0, WidgetKind::Panel, "Hidden", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			hiddenParent.Visible = false;
			result = run({ hiddenParent, layout_widget(2, 1, WidgetKind::Panel, "Child", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }) });
			check(result.Visible[0] == 0 && result.Visible[1] == 0, "layout_visibility: a hidden widget hides its children");

			UiWidget half = layout_widget(1, 0, WidgetKind::Panel, "Half", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			half.Opacity = 0.5f;
			UiWidget halfChild = layout_widget(2, 1, WidgetKind::Panel, "Child", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			halfChild.Opacity = 0.5f;
			result = run({ half, halfChild });
			check(std::abs(result.Opacity[1] - 0.25f) < 1e-5f, "layout_opacity: opacity multiplies down the tree");

			// a parent may come after its child in the array
			result = run({ layout_widget(2, 1, WidgetKind::Panel, "Child", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }),
				layout_widget(1, 0, WidgetKind::Panel, "Parent", { 0, 0 }, { 0.5f, 0.5f }, { 0, 0 }, { 0, 0 }) });
			check(near_rect(result.Rects[0], { 0, 0 }, { 960, 540 }), "layout_order: a child listed before its parent is laid out inside it");

			// stacks
			UiWidget vstack = layout_widget(1, 0, WidgetKind::VStack, "Menu", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 300, 300 });
			vstack.Spacing = 10.0f;
			vstack.Padding = { 5, 5, 5, 5 };
			std::vector<UiWidget> column = { vstack };
			for (UiId i = 0; i < 3; i++)
			{
				column.push_back(layout_widget(2 + i, 1, WidgetKind::Panel, "Item", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 }));
			}
			result = run(column);
			check(near_rect(result.Rects[1], { 5, 5 }, { 105, 35 }) && near_rect(result.Rects[2], { 5, 45 }, { 105, 75 }) && near_rect(result.Rects[3], { 5, 85 }, { 105, 115 }), "layout_vstack: items stack with the spacing, inside the padding");

			UiWidget hstack = vstack;
			hstack.Kind = WidgetKind::HStack;
			column[0] = hstack;
			result = run(column);
			check(near_rect(result.Rects[1], { 5, 5 }, { 105, 35 }) && near_rect(result.Rects[2], { 115, 5 }, { 215, 35 }) && near_rect(result.Rects[3], { 225, 5 }, { 325, 35 }), "layout_hstack: items go left to right");

			vstack.ChildAlign = UiChildAlign::Center;
			column[0] = vstack;
			result = run(column);
			check(near_rect(result.Rects[1], { 100, 5 }, { 200, 35 }), "layout_stack_align: Center centres the items across the stack");

			vstack.ChildAlign = UiChildAlign::Start;
			vstack.ForceExpand = true;
			column[0] = vstack;
			result = run(column);
			check(std::abs((result.Rects[1].Max.x - result.Rects[1].Min.x) - 290.0f) < 0.5f && std::abs((result.Rects[3].Max.y) - 295.0f) < 1.0f, "layout_stack_force_expand: items fill the cross axis and share the free main space");

			UiWidget hiddenItem = column[2];
			hiddenItem.Visible = false;
			vstack.ForceExpand = false;
			column[0] = vstack;
			column[2] = hiddenItem;
			result = run(column);
			check(near_rect(result.Rects[3], { 5, 45 }, { 105, 75 }), "layout_stack_hidden_item: a hidden item takes no room");

			result = run({ layout_widget(1, 0, WidgetKind::VStack, "Empty", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 100 }) });
			check(finite(result.Rects[0]) && result.Visible[0] == 1, "layout_stack_empty: a stack with no children is fine");

			if (font != nullptr)
			{
				UiWidget text = layout_widget(2, 1, WidgetKind::Text, "Label", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 });
				text.Text = "Hello";
				text.FontSize = 24.0f;
				result = run({ layout_widget(1, 0, WidgetKind::VStack, "Menu", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 300, 300 }), text });
				const glm::vec2 measured = measure_text(*font, "Hello", 24.0f, 0.0f, false);
				check(std::abs((result.Rects[1].Max.x - result.Rects[1].Min.x) - measured.x) < 0.5f && std::abs((result.Rects[1].Max.y - result.Rects[1].Min.y) - measured.y) < 0.5f, "layout_text_in_stack: a text without a size measures its content");
			}

			// world anchors
			UiWidget floating = layout_widget(1, 0, WidgetKind::Panel, "Marker", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 });
			floating.WorldEntity = target;
			targetPosition = { 0.0f, 0.0f, 0.0f };
			result = run({ floating });
			check(result.Visible[0] == 1 && near_rect(result.Rects[0], { 910, 525 }, { 1010, 555 }, 1.0f), "layout_world_center: an entity in front of the camera puts the pivot at the middle of the screen");
			check(std::abs(result.Distance[0] - 5.0f) < 1e-3f && result.Distance[0] == result.Distance[0], "layout_world_distance_reported: the layout says how far the anchored entity is from the camera");

			floating.WorldScreenOffset = { 0.0f, -100.0f };
			result = run({ floating });
			check(near_rect(result.Rects[0], { 910, 425 }, { 1010, 455 }, 1.0f), "layout_world_screen_offset: the screen offset moves the widget");
			floating.WorldScreenOffset = { 0.0f, 0.0f };

			floating.WorldOffset = { 0.0f, 1.0f, 0.0f };
			result = run({ floating });
			check(result.Visible[0] == 1 && result.Rects[0].Min.y < 525.0f, "layout_world_offset: a world offset up puts the widget higher on the screen");
			floating.WorldOffset = { 0.0f, 0.0f, 0.0f };

			UiWidget child = layout_widget(2, 1, WidgetKind::Panel, "Inside", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			result = run({ floating, child });
			check(near_rect(result.Rects[1], result.Rects[0].Min, result.Rects[0].Max), "layout_world_children: the children of an anchored widget follow it");

			targetPosition = { 0.0f, 0.0f, 10.0f }; // behind the camera
			result = run({ floating });
			check(result.Visible[0] == 0, "layout_world_behind: an entity behind the camera is hidden");

			targetPosition = { 0.0f, 0.0f, 0.0f };
			targetExists = false;
			result = run({ floating });
			check(result.Visible[0] == 0 && finite(result.Rects[0]), "layout_world_missing_entity: an entity that does not exist hides the widget");
			targetExists = true;

			targetPosition = { 100.0f, 0.0f, 0.0f };
			result = run({ floating });
			check(result.Visible[0] == 0, "layout_world_offscreen: off the screen with Hide hides the widget");
			floating.WorldOffscreen = UiOffscreen::ClampToEdge;
			result = run({ floating });
			check(result.Visible[0] == 1 && std::abs(result.Rects[0].Min.x + 50.0f - 1920.0f) < 1.0f, "layout_world_offscreen: ClampToEdge puts the pivot on the edge of the canvas");
			floating.WorldOffscreen = UiOffscreen::Hide;

			floating.WorldScaleWithDistance = true;
			targetPosition = { 0.0f, 0.0f, -15.0f }; // 20 away, the reference distance is 10
			result = run({ floating });
			check(std::abs((result.Rects[0].Max.x - result.Rects[0].Min.x) - 50.0f) < 0.5f && std::abs(result.Scale[0] - 0.5f) < 1e-3f, "layout_world_distance_scale: twice the reference distance is half the size");
			targetPosition = { 0.0f, 0.0f, -95.0f }; // 100 away
			result = run({ floating });
			check(std::abs(result.Scale[0] - 0.25f) < 1e-3f, "layout_world_distance_scale: the scale stops at the minimum");
			targetPosition = { 0.0f, 0.0f, 4.0f }; // 1 away
			result = run({ floating });
			check(std::abs(result.Scale[0] - 2.0f) < 1e-3f, "layout_world_distance_scale: the scale stops at the maximum");

			floating.WorldScaleWithDistance = false;
			floating.WorldFadeStart = 10.0f;
			floating.WorldFadeEnd = 30.0f;
			targetPosition = { 0.0f, 0.0f, -15.0f };
			result = run({ floating });
			check(std::abs(result.Opacity[0] - 0.5f) < 1e-3f, "layout_world_fade: halfway through the fade range is half opaque");
			targetPosition = { 0.0f, 0.0f, -40.0f };
			result = run({ floating });
			check(result.Visible[0] == 0 || result.Opacity[0] == 0.0f, "layout_world_fade: past the end of the fade range nothing is left");

			UiLayoutInput noCamera = input;
			noCamera.Camera = nullptr;
			UiPanelData lone;
			lone.Widgets = { floating };
			check(compute_layout(lone, noCamera).Visible[0] == 0, "layout_world_no_camera: without a camera a world anchored widget is hidden");

			// a canvas with no area
			for (const glm::vec2 size : { glm::vec2(0.0f, 1080.0f), glm::vec2(1920.0f, 0.0f), glm::vec2(0.0f, 0.0f) })
			{
				result = run({ layout_widget(1, 0, WidgetKind::Panel, "Fill", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }), floating }, size);
				check(result.Visible[0] == 0 && result.Visible[1] == 0 && finite(result.Rects[0]) && finite(result.Rects[1]) && std::isfinite(result.CanvasScale), "layout_zero_canvas: a canvas without area hides everything and makes no NaN");
			}
		}

		u32 pack_color(float r, float g, float b, float a)
		{
			const auto channel = [](float v) { return static_cast<u32>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
			return channel(r) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24);
		}

		bool all_finite(const UiDrawList& list)
		{
			for (const UiVertex& vertex : list.Vertices)
			{
				if (!std::isfinite(vertex.Pos.x) || !std::isfinite(vertex.Pos.y) || !std::isfinite(vertex.Uv.x) || !std::isfinite(vertex.Uv.y))
				{
					return false;
				}
			}
			return true;
		}

		// The extent of the quad that starts at vertex 4 * quad.
		UiRect quad_rect(const UiDrawList& list, size_t quad)
		{
			UiRect rect{ list.Vertices[quad * 4].Pos, list.Vertices[quad * 4].Pos };
			for (size_t i = 0; i < 4; i++)
			{
				rect.Min = glm::min(rect.Min, list.Vertices[quad * 4 + i].Pos);
				rect.Max = glm::max(rect.Max, list.Vertices[quad * 4 + i].Pos);
			}
			return rect;
		}

		void check_draw(Check& check, const FontData* font)
		{
			const glm::vec2 canvas{ 1920.0f, 1080.0f };
			const EntityGuid target = EntityGuid::generate();
			EntityGuid farTarget = EntityGuid::generate();
			const UiCameraView camera = test_camera();

			UiLayoutInput input;
			input.CanvasSize = canvas;
			input.Camera = &camera;
			input.WorldPosition = [&](const EntityGuid& guid) -> std::optional<glm::vec3>
			{
				if (guid == target)
				{
					return glm::vec3(0.0f, 0.0f, 0.0f);
				}
				if (guid == farTarget)
				{
					return glm::vec3(0.0f, 0.0f, -20.0f);
				}
				return std::nullopt;
			};
			input.Fonts = [font](const AssetRef&) { return font; };
			input.TextureSize = [](const AssetRef&) -> std::optional<glm::vec2> { return glm::vec2(64.0f, 64.0f); };

			const auto build = [&](std::vector<UiWidget> widgets, std::vector<UiInteractionState> interaction = {}, const UiLayoutInput* override = nullptr)
			{
				UiPanelData panel;
				panel.Widgets = std::move(widgets);
				const UiLayoutInput& use = override != nullptr ? *override : input;
				interaction.resize(panel.Widgets.size(), UiInteractionState::Normal);
				const UiLayoutResult layout = compute_layout(panel, use);
				UiDrawList list;
				append_panel(list, panel, interaction, layout, use);
				return list;
			};

			const AssetRef someFont{ AssetGuid::generate(), 0 };
			const AssetRef someTexture{ AssetGuid::generate(), 0 };

			check(build({}).Vertices.empty() && build({}).Batches.empty(), "draw_empty_panel: no widgets, nothing to draw");

			UiWidget red = layout_widget(1, 0, WidgetKind::Panel, "Red", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			red.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
			UiDrawList list = build({ red });
			check(list.Vertices.size() == 4 && list.Indices.size() == 6 && list.Batches.size() == 1 && list.Batches[0].Kind == UiQuadKind::Solid, "draw_solid_quad: a panel is one solid quad");
			check(list.Vertices.size() == 4 && near_rect(quad_rect(list, 0), { 0, 0 }, canvas) && list.Vertices[0].Color == pack_color(1, 0, 0, 1), "draw_solid_quad: the quad is the rect, in the colour of the widget");
			check(list.Batches.size() == 1 && list.Batches[0].FirstIndex == 0 && list.Batches[0].IndexCount == 6 && list.CanvasSize == canvas, "draw_solid_quad: the batch covers the indices, and the list knows the canvas");
			bool indicesInRange = true;
			for (const u32 index : list.Indices)
			{
				indicesInRange = indicesInRange && index < list.Vertices.size();
			}
			check(indicesInRange, "draw_solid_quad: every index points at a vertex");

			UiWidget clear = red;
			clear.Color = { 1.0f, 0.0f, 0.0f, 0.0f };
			check(build({ clear }).Vertices.empty(), "draw_clear_skipped: a fully transparent background draws nothing");

			UiWidget green = layout_widget(2, 0, WidgetKind::Panel, "Green", { 0, 0 }, { 0.5f, 0.5f }, { 0, 0 }, { 0, 0 });
			green.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
			list = build({ red, green });
			check(list.Batches.size() == 1 && list.Batches[0].IndexCount == 12 && list.Vertices.size() == 8, "draw_batch_merge: two solid siblings of one colour share a batch");
			green.Color = { 0.0f, 1.0f, 0.0f, 1.0f };
			check(build({ red, green }).Batches.size() == 1, "draw_batch_merge: the colour is in the vertices, so different colours still share a batch");

			UiWidget hidden = red;
			hidden.Visible = false;
			check(build({ hidden }).Vertices.empty(), "draw_hidden_skipped: a hidden widget draws nothing");
			UiWidget transparent = red;
			transparent.Opacity = 0.0f;
			check(build({ transparent }).Vertices.empty(), "draw_opacity_zero_skipped: an opacity of 0 draws nothing");
			UiWidget faint = red;
			faint.Opacity = 0.5f;
			list = build({ faint });
			check(list.Vertices.size() == 4 && list.Vertices[0].Color == pack_color(1, 0, 0, 0.5f), "draw_opacity: opacity multiplies the alpha");

			// text
			if (font != nullptr)
			{
				UiWidget text = layout_widget(1, 0, WidgetKind::Text, "Label", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 400, 60 });
				text.Text = "AB";
				text.Font = someFont;
				text.FontSize = 24.0f;
				text.Outline = 0.25f;
				text.Shadow = true;
				list = build({ text });
				check(list.Vertices.size() == 8 && list.Indices.size() == 12 && list.Batches.size() == 1 && list.Batches[0].Kind == UiQuadKind::Sdf && list.Batches[0].Texture == someFont,
					"draw_text_batch: text is one quad per glyph in an SDF batch that names the font");
				check(list.Batches.size() == 1 && list.Batches[0].SdfRange == font->SdfRange && std::abs(list.Batches[0].PixelScale - 24.0f / font->PixelSize) < 1e-4f && list.Batches[0].Outline == 0.25f && list.Batches[0].Shadow,
					"draw_text_batch: the batch carries the range, the pixel scale, the outline and the shadow");

				text.Text = "";
				check(build({ text }).Vertices.empty(), "draw_text_empty: no text, no quads");

				text.Text = "AB";
				UiLayoutInput noFont = input;
				noFont.Fonts = [](const AssetRef&) -> const FontData* { return nullptr; };
				check(build({ text }, {}, &noFont).Vertices.empty(), "draw_text_missing_font: while the font is not loaded the text is not drawn");

				UiWidget colored = text;
				colored.Color = { 0.0f, 0.0f, 1.0f, 1.0f };
				list = build({ colored });
				check(list.Vertices.size() == 8 && list.Vertices[0].Color == pack_color(0, 0, 1, 1), "draw_text_colour: glyphs take the colour of the widget");
			}

			// buttons
			UiWidget button = layout_widget(1, 0, WidgetKind::Button, "Go", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 });
			button.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
			button.HoverColor = { 0.0f, 1.0f, 0.0f, 1.0f };
			button.PressedColor = { 0.0f, 0.0f, 1.0f, 1.0f };
			button.DisabledColor = { 0.5f, 0.5f, 0.5f, 0.5f };
			check(build({ button }).Vertices[0].Color == pack_color(1, 1, 1, 1), "draw_button_states: normal uses Color");
			check(build({ button }, { UiInteractionState::Hover }).Vertices[0].Color == pack_color(0, 1, 0, 1), "draw_button_states: hover uses HoverColor");
			check(build({ button }, { UiInteractionState::Pressed }).Vertices[0].Color == pack_color(0, 0, 1, 1), "draw_button_states: pressed uses PressedColor");
			UiWidget disabledButton = button;
			disabledButton.Enabled = false;
			check(build({ disabledButton }, { UiInteractionState::Pressed }).Vertices[0].Color == pack_color(0.5f, 0.5f, 0.5f, 0.5f), "draw_button_states: disabled uses DisabledColor whatever the pointer does");

			// bars and sliders
			UiWidget bar = layout_widget(1, 0, WidgetKind::Bar, "Health", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 200, 20 });
			bar.Value = 0.25f;
			list = build({ bar });
			check(list.Vertices.size() == 8 && near_rect(quad_rect(list, 0), { 0, 0 }, { 200, 20 }) && near_rect(quad_rect(list, 1), { 0, 0 }, { 50, 20 }), "draw_bar_fill: a quarter full bar fills a quarter of its width, from the left");
			bar.Direction = UiDirection::RightToLeft;
			list = build({ bar });
			check(list.Vertices.size() == 8 && near_rect(quad_rect(list, 1), { 150, 0 }, { 200, 20 }), "draw_bar_fill: RightToLeft fills from the right");
			bar.Direction = UiDirection::BottomToTop;
			list = build({ bar });
			check(list.Vertices.size() == 8 && near_rect(quad_rect(list, 1), { 0, 15 }, { 200, 20 }), "draw_bar_fill: BottomToTop fills from the bottom");
			bar.Direction = UiDirection::LeftToRight;
			bar.Value = 5.0f;
			list = build({ bar });
			check(list.Vertices.size() == 8 && near_rect(quad_rect(list, 1), { 0, 0 }, { 200, 20 }), "draw_bar_clamp: a value above 1 is a full bar");
			bar.Value = -1.0f;
			check(build({ bar }).Vertices.size() == 4, "draw_bar_clamp: a value below 0 is an empty bar (only the background)");
			bar.Value = std::numeric_limits<float>::quiet_NaN();
			list = build({ bar });
			check(all_finite(list) && list.Vertices.size() == 4, "draw_bar_clamp: NaN is an empty bar");

			UiWidget slider = layout_widget(1, 0, WidgetKind::Slider, "Volume", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 200, 20 });
			slider.MinValue = 0.0f;
			slider.MaxValue = 10.0f;
			slider.Value = 5.0f;
			list = build({ slider });
			check(list.Vertices.size() == 12 && all_finite(list) && near_rect(quad_rect(list, 1), { 0, 0 }, { 100, 20 }), "draw_slider: track, fill up to the value, and a handle");
			slider.MinValue = 3.0f;
			slider.MaxValue = 3.0f;
			slider.Step = 0.0f;
			list = build({ slider });
			check(all_finite(list) && !list.Vertices.empty(), "draw_slider_degenerate: min equal to max, and no step, give finite quads");
			slider.MaxValue = std::numeric_limits<float>::infinity();
			check(all_finite(build({ slider })), "draw_slider_degenerate: an infinite range gives finite quads");

			UiWidget toggle = layout_widget(1, 0, WidgetKind::Toggle, "Mute", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 30, 30 });
			toggle.Value = 0.0f;
			check(build({ toggle }).Vertices.size() == 4, "draw_toggle: off is the box");
			toggle.Value = 1.0f;
			check(build({ toggle }).Vertices.size() == 8, "draw_toggle: on adds the check");

			// images
			UiWidget image = layout_widget(1, 0, WidgetKind::Image, "Logo", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 128, 128 });
			image.Texture = someTexture;
			list = build({ image });
			check(list.Batches.size() == 1 && list.Batches[0].Kind == UiQuadKind::Image && list.Batches[0].Texture == someTexture && list.Vertices.size() == 4, "draw_image: an image is one quad that names the texture");
			image.Slice9 = true;
			image.SliceBorders = { 16.0f, 16.0f, 16.0f, 16.0f };
			list = build({ image });
			check(list.Vertices.size() == 36 && list.Batches.size() == 1 && near_rect(quad_rect(list, 4), { 16, 16 }, { 112, 112 }) && all_finite(list), "draw_slice9: nine quads, the middle one inside the borders");
			image.SliceBorders = { 100.0f, 100.0f, 100.0f, 100.0f };
			check(all_finite(build({ image })), "draw_slice9: borders bigger than the widget stay finite");
			UiWidget plain = image;
			plain.Texture = AssetRef{};
			plain.Slice9 = false;
			check(build({ plain }).Batches.size() == 1 && build({ plain }).Batches[0].Kind == UiQuadKind::Solid, "draw_image_no_texture: an image without a texture is a solid quad");

			// clipping
			UiWidget clipper = layout_widget(1, 0, WidgetKind::Panel, "Clip", { 0, 0 }, { 0, 0 }, { 100, 100 }, { 300, 200 });
			clipper.ClipChildren = true;
			clipper.Color = { 0.0f, 0.0f, 0.0f, 1.0f };
			UiWidget inside = layout_widget(2, 1, WidgetKind::Panel, "Inside", { 0, 0 }, { 1, 1 }, { -50, -50 }, { 50, 50 });
			inside.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
			list = build({ clipper, inside });
			check(list.Batches.size() == 2 && list.Batches[0].ClipRect == glm::vec4(0, 0, 1920, 1080) && list.Batches[1].ClipRect == glm::vec4(100, 100, 300, 200), "draw_clip: a child of a clipping widget carries its rect, the widget itself is not clipped");

			// world anchors
			UiWidget nearMarker = layout_widget(1, 0, WidgetKind::Panel, "Near", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 40, 40 });
			nearMarker.WorldEntity = target;
			nearMarker.WorldScreenOffset = { 200.0f, 0.0f };
			nearMarker.Color = { 0.0f, 0.0f, 1.0f, 1.0f };
			UiWidget farMarker = layout_widget(2, 0, WidgetKind::Panel, "Far", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 40, 40 });
			farMarker.WorldEntity = farTarget;
			farMarker.WorldScreenOffset = { -200.0f, 0.0f };
			farMarker.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
			list = build({ nearMarker, farMarker });
			check(list.Batches.size() == 1 && list.Vertices.size() == 8 && list.Vertices[0].Color == pack_color(1, 0, 0, 1) && list.Vertices[4].Color == pack_color(0, 0, 1, 1), "draw_order_world: the farther world widget is drawn first, whatever the order in the panel");

			UiWidget backdrop = layout_widget(3, 0, WidgetKind::Panel, "Backdrop", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			backdrop.Color = { 0.0f, 1.0f, 0.0f, 1.0f };
			list = build({ nearMarker, backdrop });
			check(list.Vertices.size() == 8 && list.Vertices[0].Color == pack_color(0, 1, 0, 1), "draw_order_world: world widgets are drawn after the widgets that are fixed to the screen");
		}

		// A texel of the font's atlas at a position given as a fraction of a glyph's quad.
		u8 atlas_texel(const FontData& font, const FontGlyph& glyph, float fx, float fy)
		{
			const float u = glyph.UvMin.x + (glyph.UvMax.x - glyph.UvMin.x) * fx;
			const float v = glyph.UvMin.y + (glyph.UvMax.y - glyph.UvMin.y) * fy;
			const u32 x = std::min(static_cast<u32>(u * static_cast<float>(font.AtlasWidth)), font.AtlasWidth - 1);
			const u32 y = std::min(static_cast<u32>(v * static_cast<float>(font.AtlasHeight)), font.AtlasHeight - 1);
			return font.Atlas[static_cast<size_t>(y) * font.AtlasWidth + x];
		}

		size_t distinct_rows(const std::vector<TextGlyphQuad>& quads)
		{
			std::vector<float> rows;
			for (const TextGlyphQuad& quad : quads)
			{
				const float row = std::round(quad.Max.y);
				if (std::find(rows.begin(), rows.end(), row) == rows.end())
				{
					rows.push_back(row);
				}
			}
			return rows.size();
		}

		void check_fonts(Check& check, AssetPipelineModule& pipeline, Handle<FontData>& handle)
		{
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();

			const std::optional<AssetRef> ref = database.ensure_imported(database.get_assets_root() / "fonts" / "Lato-Regular.ttf");
			check(ref.has_value(), "font_imported: the shipped font imports");
			if (!ref)
			{
				return;
			}
			handle = manager.load<FontData>(*ref);
			manager.wait_idle();
			const FontData* fontPointer = handle.get();
			check(fontPointer != nullptr, "font_imported: the cooked font loads");
			if (fontPointer == nullptr)
			{
				return;
			}
			const FontData& font = *fontPointer;

			const FontGlyph* letterA = font.find('A');
			const FontGlyph* letterI = font.find('I');
			check(letterA != nullptr && letterA->Advance > 0.0f, "font_imported: the glyph A is there with an advance");
			check(font.find(' ') != nullptr && font.find(0xE9) != nullptr && font.find(0x4E2D) == nullptr, "font_imported: ASCII and Latin-1 are in, other scripts are not");
			check(font.AtlasWidth > 0 && font.AtlasHeight > 0 && font.Atlas.size() == static_cast<size_t>(font.AtlasWidth) * font.AtlasHeight, "font_imported: the atlas is one byte per texel");
			check(font.PixelSize > 0.0f && font.SdfRange > 0.0f && font.LineHeight > font.PixelSize * 0.5f && font.Ascent > 0.0f, "font_imported: the metrics are set");

			if (letterI != nullptr)
			{
				check(atlas_texel(font, *letterI, 0.5f, 0.5f) > 128, "font_atlas_has_sdf: inside the stem of an I is above the edge value");
				check(atlas_texel(font, *letterI, 0.0f, 0.0f) < 128, "font_atlas_has_sdf: the corner of the quad, outside the glyph, is below the edge value");
			}

			// every glyph lies inside the atlas and no two share texels
			bool inside = true;
			std::vector<u8> used(font.Atlas.size(), 0);
			bool overlap = false;
			for (const FontGlyph& glyph : font.Glyphs)
			{
				inside = inside && glyph.UvMin.x >= 0.0f && glyph.UvMin.y >= 0.0f && glyph.UvMax.x <= 1.0f && glyph.UvMax.y <= 1.0f;
				const u32 x0 = static_cast<u32>(std::round(glyph.UvMin.x * static_cast<float>(font.AtlasWidth)));
				const u32 x1 = static_cast<u32>(std::round(glyph.UvMax.x * static_cast<float>(font.AtlasWidth)));
				const u32 y0 = static_cast<u32>(std::round(glyph.UvMin.y * static_cast<float>(font.AtlasHeight)));
				const u32 y1 = static_cast<u32>(std::round(glyph.UvMax.y * static_cast<float>(font.AtlasHeight)));
				for (u32 y = y0; y < y1 && y < font.AtlasHeight; y++)
				{
					for (u32 x = x0; x < x1 && x < font.AtlasWidth; x++)
					{
						u8& flag = used[static_cast<size_t>(y) * font.AtlasWidth + x];
						overlap = overlap || flag != 0;
						flag = 1;
					}
				}
			}
			check(inside, "font_atlas_packing: every glyph is inside the atlas");
			check(!overlap, "font_atlas_packing: no two glyphs share texels");

			const std::vector<std::byte> blob = serialize(font);
			const std::optional<FontData> back = deserialize<FontData>(blob);
			check(back && back->Atlas == font.Atlas && back->Glyphs.size() == font.Glyphs.size() && back->AtlasWidth == font.AtlasWidth && back->LineHeight == font.LineHeight,
				"font_roundtrip: the atlas, the glyphs and the metrics survive");
			check(!deserialize<FontData>(std::span<const std::byte>(blob.data(), blob.size() / 2)).has_value(), "a truncated cooked font is rejected");

			// text
			const float margin = font.SdfRange * (24.0f / font.PixelSize) + 1.0f; // a quad reaches the SDF margin past the pen box
			const glm::vec2 small = measure_text(font, "Hello", 24.0f, 0.0f, false);
			const glm::vec2 large = measure_text(font, "Hello", 48.0f, 0.0f, false);
			const glm::vec2 longer = measure_text(font, "Hello world", 24.0f, 0.0f, false);
			check(small.x > 0.0f && longer.x > small.x, "text_measure_monotonic: a longer string is wider");
			check(std::abs(large.x - 2.0f * small.x) < small.x * 0.01f && std::abs(large.y - 2.0f * small.y) < small.y * 0.01f, "text_measure_monotonic: twice the size is twice the extent");

			const glm::vec2 none = measure_text(font, "", 24.0f, 0.0f, false);
			check(none.x == 0.0f && layout_text(font, "", 24.0f, { 100.0f, 50.0f }, UiTextAlign::Left, false).empty(), "text_empty: an empty string has no size and no quads");

			const std::vector<TextGlyphQuad> single = layout_text(font, "AB", 24.0f, { 200.0f, 50.0f }, UiTextAlign::Left, false);
			check(single.size() == 2 && single[1].Min.x > single[0].Min.x, "text_layout: one quad per visible glyph, in order");

			const std::vector<TextGlyphQuad> twoWords = layout_text(font, "hello world hello world", 24.0f, { 100.0f, 200.0f }, UiTextAlign::Left, true);
			check(distinct_rows(twoWords) >= 2, "text_wrap: a box narrower than the text makes more than one line");
			bool wrapFits = !twoWords.empty();
			for (const TextGlyphQuad& quad : twoWords)
			{
				wrapFits = wrapFits && quad.Max.x <= 100.0f + margin;
			}
			check(wrapFits, "text_wrap: wrapped lines stay inside the box");
			check(distinct_rows(layout_text(font, "hello world hello world", 24.0f, { 100.0f, 200.0f }, UiTextAlign::Left, false)) == 1, "text_wrap: without Wrap it stays on one line");
			check(distinct_rows(layout_text(font, "one\ntwo", 24.0f, { 500.0f, 200.0f }, UiTextAlign::Left, false)) == 2, "text_wrap: a line break starts a new line");

			const std::vector<TextGlyphQuad> longWord = layout_text(font, "WWWWWWWWWWWWWWWWWWWWWWWWWWWW", 24.0f, { 50.0f, 400.0f }, UiTextAlign::Left, true);
			bool longFits = !longWord.empty() && distinct_rows(longWord) >= 2;
			for (const TextGlyphQuad& quad : longWord)
			{
				longFits = longFits && quad.Max.x <= 50.0f + margin;
			}
			check(longFits && longWord.size() == 28, "text_wrap: a word wider than the box breaks by character and every glyph is drawn");

			const std::vector<TextGlyphQuad> missing = layout_text(font, "\xE4\xB8\xAD", 24.0f, { 100.0f, 50.0f }, UiTextAlign::Left, false);
			const std::vector<TextGlyphQuad> question = layout_text(font, "?", 24.0f, { 100.0f, 50.0f }, UiTextAlign::Left, false);
			check(missing.size() == 1 && question.size() == 1 && missing[0].UvMin == question[0].UvMin, "text_missing_glyph: a glyph the font lacks draws the fallback");

			check(layout_text(font, "\xC3\x28", 24.0f, { 100.0f, 50.0f }, UiTextAlign::Left, false).size() >= 1, "text_invalid_utf8: broken UTF-8 draws something and does not crash");
			check(!layout_text(font, "\xFF\xFE\xF0\x9F", 24.0f, { 100.0f, 50.0f }, UiTextAlign::Left, true).empty(), "text_invalid_utf8: bytes that are never UTF-8 are replaced");

			if (letterA != nullptr)
			{
				const float scale = 24.0f / font.PixelSize;
				const glm::vec2 width = measure_text(font, "AB", 24.0f, 0.0f, false);
				const std::vector<TextGlyphQuad> centered = layout_text(font, "AB", 24.0f, { 400.0f, 50.0f }, UiTextAlign::Center, false);
				const std::vector<TextGlyphQuad> right = layout_text(font, "AB", 24.0f, { 400.0f, 50.0f }, UiTextAlign::Right, false);
				check(centered.size() == 2 && std::abs((centered[0].Min.x - letterA->Bearing.x * scale) - (400.0f - width.x) * 0.5f) < 1.0f, "text_align: centered text starts half the free width in");
				check(right.size() == 2 && std::abs((right[0].Min.x - letterA->Bearing.x * scale) - (400.0f - width.x)) < 1.0f, "text_align: right aligned text ends at the box edge");
			}
		}

		void check_field_table(Check& check)
		{
			const UiWidget defaults;
			for (const UiField& field : ui_fields())
			{
				check(field.Get(defaults) == field.Default, "the default of field " + field.Name + " is what a default widget holds");
				UiWidget copy;
				check(field.Set(copy, field.Get(defaults)), "field " + field.Name + " accepts its own value");
				check(!field.Set(copy, FieldValue(std::in_place_type<std::vector<AssetRef>>)), "field " + field.Name + " rejects a value of another type");
			}
			check(find_ui_field("Opacity") != nullptr && find_ui_field("Nope") == nullptr, "fields are found by name");
			check(ui_field_visible(WidgetKind::Text, "Text") && !ui_field_visible(WidgetKind::Bar, "Text"), "a Text widget shows Text and a Bar does not");
			check(ui_field_visible(WidgetKind::Bar, "WorldEntity") && ui_field_visible(WidgetKind::Panel, "OffsetMin"), "every kind shows the rect and the world anchor");
		}
	}

	bool run_ui_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_field_table(check);
		check_panel_files(check);
		Handle<FontData> fontHandle;
		check_fonts(check, pipeline, fontHandle);
		check_layout(check, fontHandle.get());
		check_draw(check, fontHandle.get());

		if (check.Failures == 0)
		{
			WI_UI_LOG(Info, "UI smoke test passed");
		}
		return check.Failures == 0;
	}
}
