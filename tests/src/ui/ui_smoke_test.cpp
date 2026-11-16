#include <editor_common.h>
#include "ui_smoke_test.h"

#include <asset_pipeline/asset_database.h>
#include <ui/editor_play_input.h>
#include <ui/ui_edit_model.h>

#include <algorithm>
#include <fstream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>

#include <assets/asset_manager.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <glm/gtc/matrix_transform.hpp>
#include <ui/ui_font.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_input.h>
#include <ui/ui_interaction.h>
#include <ui/ui_layout.h>
#include <ui/ui_module.h>
#include <renderer/scene/render_scene.h>
#include <scene/scene.h>
#include <ui/ui_panel_io.h>
#include <ui/ui_video_fit.h>
#include <ui/ui_widget.h>

#define WI_UI_LOG(level, ...) WI_LOG(level, "UI", __VA_ARGS__)

#include <test_kit.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "UI";
			static constexpr const char* Prefix = "UI smoke test";
		};
		using Check = BasicCheck<CheckLog>;

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

			UiWidget video = make_widget(9, 0, WidgetKind::Video, "Intro");
			video.Video = AssetRef{ AssetGuid::generate(), 0 };
			video.Fit = UiVideoFit::Cover;
			video.Loop = true;
			video.AutoPlay = false;
			video.Volume = 0.4f;
			video.LetterboxColor = { 0.1f, 0.2f, 0.3f, 0.9f };
			panel.Widgets.push_back(video);

			panel.NextId = 10;
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

		// A panel, a scripted pointer and the interaction state machine, stepped by hand.
		struct Rig
		{
			UiPanelData Panel{};
			UiInteraction Interaction{};
			std::vector<UiInteractionState> States{};
			ScriptedInputSource Source{};
			std::vector<UiEvent> Events{};
			UiLayoutInput Input{};
			UiCameraView Camera{};
			glm::vec3 AnchorPosition{ 0.0f };
			EntityGuid Anchor{};

			Rig()
			{
				Input.CanvasSize = { 1920.0f, 1080.0f };
				Camera = test_camera();
				Anchor = EntityGuid::generate();
				Input.Camera = &Camera;
				Input.WorldPosition = [this](const EntityGuid& guid) -> std::optional<glm::vec3>
				{
					return guid == Anchor ? std::optional<glm::vec3>(AnchorPosition) : std::nullopt;
				};
			}

			void step()
			{
				const UiLayoutResult layout = compute_layout(Panel, Input);
				States.resize(Panel.Widgets.size(), UiInteractionState::Normal);
				Interaction.update(Panel, layout, States, Source.poll(), Events);
			}

			size_t count(UiEventKind kind, UiId widget) const
			{
				return static_cast<size_t>(std::count_if(Events.begin(), Events.end(), [&](const UiEvent& event) { return event.Kind == kind && event.Widget == widget; }));
			}

			UiWidget& widget(UiId id) { return *Panel.find(id); }
		};

		UiWidget button_at(UiId id, float x, float y, float w = 100.0f, float h = 30.0f)
		{
			return layout_widget(id, 0, WidgetKind::Button, "Button", { 0, 0 }, { 0, 0 }, { x, y }, { x + w, y + h });
		}

		void check_interaction(Check& check)
		{
			// the scripted source
			{
				ScriptedInputSource source;
				UiInputFrame frame = source.poll();
				check(!frame.PointerValid && !frame.Down[0] && !frame.Pressed[0] && frame.NavPressed == 0, "input_scripted: a fresh source has no pointer and nothing pressed");
				source.move_to({ 10.0f, 20.0f });
				source.press(0);
				frame = source.poll();
				check(frame.PointerValid && frame.Pointer == glm::vec2(10.0f, 20.0f) && frame.Down[0] && frame.Pressed[0] && !frame.Released[0], "input_scripted: a press is down, and a press edge");
				frame = source.poll();
				check(frame.Down[0] && !frame.Pressed[0] && frame.PointerValid, "input_scripted: the press edge lasts one poll, the button stays down");
				source.release(0);
				frame = source.poll();
				check(!frame.Down[0] && frame.Released[0], "input_scripted: a release is up, and a release edge");
				frame = source.poll();
				check(!frame.Down[0] && !frame.Released[0], "input_scripted: the release edge lasts one poll");
				source.press(0);
				source.release(0);
				frame = source.poll();
				check(frame.Pressed[0] && frame.Released[0] && !frame.Down[0], "input_scripted: a press and a release between two polls are both seen");
				source.nav(UiNav::Down);
				source.nav(UiNav::Submit);
				frame = source.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Down)) != 0 && (frame.NavPressed & ui_nav_bit(UiNav::Submit)) != 0 && (frame.NavPressed & ui_nav_bit(UiNav::Up)) == 0 && source.poll().NavPressed == 0, "input_scripted: navigation presses last one poll");
				source.press(7);
				check(!source.poll().Pressed[0], "input_scripted: a button that does not exist is ignored");
				NullInputSource none;
				check(!none.poll().PointerValid, "input_null: the null source has no pointer");
			}

			// hover and click
			{
				Rig rig;
				rig.Panel.Widgets = { button_at(1, 100, 100) };
				rig.Source.move_to({ 150.0f, 115.0f });
				rig.step();
				check(rig.count(UiEventKind::HoverEnter, 1) == 1 && rig.States[0] == UiInteractionState::Hover, "ui_hover_enter_exit: moving onto a button hovers it");
				rig.step();
				check(rig.count(UiEventKind::HoverEnter, 1) == 1, "ui_hover_enter_exit: hovering again says nothing new");
				rig.Source.move_to({ 500.0f, 500.0f });
				rig.step();
				check(rig.count(UiEventKind::HoverExit, 1) == 1 && rig.States[0] == UiInteractionState::Normal, "ui_hover_enter_exit: moving away ends the hover");

				rig.Source.move_to({ 150.0f, 115.0f });
				rig.step();
				rig.Source.press(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 0 && rig.States[0] == UiInteractionState::Pressed, "ui_click_fires_on_release: pressing does not click, and shows pressed");
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 1 && rig.States[0] == UiInteractionState::Hover, "ui_click_fires_on_release: releasing over the button clicks it once");
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 1, "ui_click_fires_on_release: and not again");

				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 2, "ui_click_one_frame: a press and a release in one frame is a click");

				rig.Source.press(0);
				rig.step();
				rig.Source.move_to({ 500.0f, 500.0f });
				rig.step();
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 2, "ui_press_release_elsewhere_no_click: pressing on a button and releasing off it is not a click");

				rig.Source.move_to({ 500.0f, 500.0f });
				rig.Source.press(0);
				rig.step();
				rig.Source.move_to({ 150.0f, 115.0f });
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 2, "ui_press_release_elsewhere_no_click: pressing off a button and releasing on it is not a click");
			}

			// what cannot be clicked
			{
				Rig rig;
				UiWidget disabled = button_at(1, 100, 100);
				disabled.Enabled = false;
				rig.Panel.Widgets = { disabled };
				rig.Source.move_to({ 150.0f, 115.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 0 && rig.count(UiEventKind::HoverEnter, 1) == 0 && rig.States[0] == UiInteractionState::Normal, "ui_disabled_not_clickable: a disabled button neither hovers nor clicks");

				UiWidget parent = layout_widget(1, 0, WidgetKind::Panel, "Parent", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
				parent.Enabled = false;
				UiWidget child = button_at(2, 100, 100);
				child.Parent = 1;
				rig.Panel.Widgets = { parent, child };
				rig.Events.clear();
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 2) == 0, "ui_disabled_not_clickable: a button in a disabled parent cannot be clicked");

				UiWidget hidden = button_at(1, 100, 100);
				hidden.Visible = false;
				rig.Panel.Widgets = { hidden };
				rig.Events.clear();
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.Events.empty(), "ui_hidden_not_clickable: a hidden button gets no events");

				UiWidget clear = button_at(1, 100, 100);
				clear.Opacity = 0.0f;
				rig.Panel.Widgets = { clear };
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.Events.empty(), "ui_hidden_not_clickable: a button with an opacity of 0 gets no events");

				UiWidget clipper = layout_widget(1, 0, WidgetKind::Panel, "Clip", { 0, 0 }, { 0, 0 }, { 100, 100 }, { 200, 200 });
				clipper.ClipChildren = true;
				UiWidget clipped = button_at(2, 0, 0, 400, 400);
				clipped.Parent = 1;
				rig.Panel.Widgets = { clipper, clipped };
				rig.Source.move_to({ 250.0f, 150.0f }); // inside the button, outside the clipping parent
				rig.step();
				check(rig.Events.empty(), "ui_clip_blocks_hit: the part of a button outside its clipping parent cannot be hit");
				rig.Source.move_to({ 150.0f, 150.0f });
				rig.step();
				check(rig.count(UiEventKind::HoverEnter, 2) == 1, "ui_clip_blocks_hit: the part inside can");
			}

			// overlap
			{
				Rig rig;
				rig.Panel.Widgets = { button_at(1, 100, 100), button_at(2, 150, 100) };
				rig.Source.move_to({ 175.0f, 115.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 2) == 1 && rig.count(UiEventKind::Click, 1) == 0, "ui_overlap_topmost_wins: the widget drawn last gets the click");
			}

			// sliders
			{
				Rig rig;
				UiWidget slider = layout_widget(1, 0, WidgetKind::Slider, "Volume", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 200, 20 });
				rig.Panel.Widgets = { slider };
				rig.Source.move_to({ 50.0f, 10.0f });
				rig.Source.press(0);
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.25f) < 1e-4f && rig.count(UiEventKind::ValueChanged, 1) == 1, "ui_slider_drag: pressing on the track sets the value there");
				rig.Source.move_to({ 500.0f, 400.0f });
				rig.step();
				check(std::abs(rig.widget(1).Value - 1.0f) < 1e-4f && rig.States[0] == UiInteractionState::Pressed, "ui_slider_drag: dragging off the end keeps following, clamped");
				rig.Source.release(0);
				rig.step();
				rig.Source.move_to({ 100.0f, 10.0f });
				rig.step();
				check(std::abs(rig.widget(1).Value - 1.0f) < 1e-4f, "ui_slider_drag: after the release the pointer no longer moves it");
				check(rig.count(UiEventKind::Click, 1) == 0, "ui_slider_drag: a slider does not click");

				rig.widget(1).Direction = UiDirection::RightToLeft;
				rig.Source.move_to({ 50.0f, 10.0f });
				rig.Source.press(0);
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.75f) < 1e-4f, "ui_slider_drag: RightToLeft counts from the right");
				rig.Source.release(0);
				rig.step();

				rig.widget(1).Direction = UiDirection::LeftToRight;
				rig.widget(1).Step = 0.25f;
				rig.Source.move_to({ 60.0f, 10.0f });
				rig.Source.press(0);
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.25f) < 1e-4f, "ui_slider_step_snap: 30 percent snaps to the step below");
				rig.Source.move_to({ 80.0f, 10.0f });
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.5f) < 1e-4f, "ui_slider_step_snap: 40 percent snaps up");
				rig.Source.release(0);
				rig.step();

				rig.widget(1).MinValue = 5.0f;
				rig.widget(1).MaxValue = 5.0f;
				rig.widget(1).Value = 5.0f;
				rig.Events.clear();
				rig.Source.move_to({ 50.0f, 10.0f });
				rig.Source.press(0);
				rig.step();
				check(rig.widget(1).Value == 5.0f && rig.count(UiEventKind::ValueChanged, 1) == 0, "ui_slider_degenerate: min equal to max never changes the value");
				rig.widget(1).MaxValue = std::numeric_limits<float>::infinity();
				rig.step();
				check(std::isfinite(rig.widget(1).Value), "ui_slider_degenerate: an infinite range never makes the value NaN or infinite");
			}

			// toggles
			{
				Rig rig;
				UiWidget toggle = layout_widget(1, 0, WidgetKind::Toggle, "Mute", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 30, 30 });
				rig.Panel.Widgets = { toggle };
				rig.Source.move_to({ 10.0f, 10.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.widget(1).Value > 0.5f && rig.count(UiEventKind::ValueChanged, 1) == 1 && rig.count(UiEventKind::Click, 1) == 1, "ui_toggle_flips: a click turns it on");
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.widget(1).Value < 0.5f && rig.count(UiEventKind::ValueChanged, 1) == 2, "ui_toggle_flips: and the next click turns it off");
			}

			// navigation
			{
				Rig rig;
				UiWidget second = button_at(2, 100, 200);
				UiWidget first = button_at(1, 100, 100);
				UiWidget third = button_at(3, 100, 300);
				rig.Panel.Widgets = { first, second, third };
				rig.Source.nav(UiNav::Down);
				rig.step();
				check(rig.Interaction.focused() == 1 && rig.count(UiEventKind::Focus, 1) == 1 && rig.States[0] == UiInteractionState::Focused, "ui_nav_moves_focus: with nothing focused, Down focuses the first widget");
				rig.Source.nav(UiNav::Down);
				rig.step();
				rig.Source.nav(UiNav::Down);
				rig.step();
				check(rig.Interaction.focused() == 3, "ui_nav_moves_focus: Down moves to the next one below");
				rig.Source.nav(UiNav::Down);
				rig.step();
				check(rig.Interaction.focused() == 3, "ui_nav_moves_focus: at the last one Down stays");
				rig.Source.nav(UiNav::Up);
				rig.step();
				check(rig.Interaction.focused() == 2, "ui_nav_moves_focus: Up goes back");
				rig.Source.nav(UiNav::Right);
				rig.step();
				check(rig.Interaction.focused() == 2, "ui_nav_moves_focus: Right with nothing to the right stays");

				rig.Source.nav(UiNav::Submit);
				rig.step();
				check(rig.count(UiEventKind::Click, 2) == 1, "ui_submit_clicks_focused: Submit clicks the focused button");

				rig.Source.nav(UiNav::Cancel);
				rig.step();
				check(rig.Interaction.focused() == 0 && rig.States[1] == UiInteractionState::Normal, "ui_nav_cancel: Cancel clears the focus");

				rig.widget(2).Enabled = false;
				rig.Source.nav(UiNav::Down);
				rig.step();
				rig.Source.nav(UiNav::Down);
				rig.step();
				check(rig.Interaction.focused() == 3, "ui_nav_skips_disabled: a disabled widget cannot take the focus");

				rig.widget(2).Enabled = true;
				rig.Source.move_to({ 150.0f, 215.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.Interaction.focused() == 2, "ui_click_focuses: clicking a widget focuses it");

				rig.widget(2).Visible = false;
				rig.Source.nav(UiNav::Submit);
				rig.step();
				check(rig.Interaction.focused() == 0 && rig.count(UiEventKind::Click, 2) == 2, "ui_nav_focus_lost: a focused widget that gets hidden loses the focus and does not click");
			}

			// a slider with the focus
			{
				Rig rig;
				UiWidget slider = layout_widget(1, 0, WidgetKind::Slider, "Volume", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 200, 20 });
				slider.Step = 0.1f;
				slider.Value = 0.5f;
				rig.Panel.Widgets = { slider };
				rig.Source.nav(UiNav::Down);
				rig.step();
				rig.Source.nav(UiNav::Right);
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.6f) < 1e-4f && rig.count(UiEventKind::ValueChanged, 1) == 1, "ui_nav_slider: Right on a focused slider adds one step");
				rig.Source.nav(UiNav::Left);
				rig.Source.nav(UiNav::Left);
				rig.step();
				check(std::abs(rig.widget(1).Value - 0.4f) < 1e-4f || std::abs(rig.widget(1).Value - 0.5f) < 1e-4f, "ui_nav_slider: Left lowers it");
			}

			// a widget that goes away while it is pressed
			{
				Rig rig;
				rig.Panel.Widgets = { button_at(1, 100, 100) };
				rig.Source.move_to({ 150.0f, 115.0f });
				rig.Source.press(0);
				rig.step();
				rig.Panel.Widgets.clear();
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 0 && rig.Interaction.captured() == 0, "ui_widget_removed_while_pressed: no click, and the capture is dropped");
			}

			// world anchored
			{
				Rig rig;
				UiWidget marker = button_at(1, 0, 0, 100, 30);
				marker.WorldEntity = rig.Anchor;
				rig.Panel.Widgets = { marker };
				rig.Source.move_to({ 960.0f, 540.0f }); // the anchor is in front of the camera: the middle of the screen
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 1, "ui_world_anchored_clickable: a button anchored in the world is hit where it is drawn");
				rig.Source.move_to({ 100.0f, 100.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.step();
				check(rig.count(UiEventKind::Click, 1) == 1, "ui_world_anchored_clickable: and not where it is not");
			}
		}

		// A scene with a camera at (0, 0, 5) looking at the origin, and the panels the loader hands out by asset.
		struct ModuleRig
		{
			Ref<Scene> TheScene = std::make_shared<Scene>();
			UiModule Module{};
			ScriptedInputSource Source{};
			std::unordered_map<AssetGuid, Ref<AssetSlot>> Slots{};
			const FontData* Font = nullptr;
			entt::entity Camera = entt::null;
			glm::vec2 Canvas{ 1920.0f, 1080.0f };

			explicit ModuleRig(const FontData* font) : Font(font)
			{
				Camera = TheScene->create("Camera");
				TheScene->registry().get<Transform>(Camera).Position = { 0.0f, 0.0f, 5.0f };
				TheScene->registry().emplace<::wiwa::Camera>(Camera).Primary = true;
				TheScene->update_world_transforms();

				Module.set_panel_loader([this](const AssetRef& ref) -> Handle<UiPanelData>
				{
					const auto it = Slots.find(ref.Guid);
					return it == Slots.end() ? Handle<UiPanelData>{} : Handle<UiPanelData>(ref, it->second);
				});
				Module.set_font_provider([this](const AssetRef&) { return Font; });
				Module.set_input_source(&Source);
			}

			AssetRef add_panel(const UiPanelData& data, LoadState state = LoadState::Ready)
			{
				const AssetRef ref{ AssetGuid::generate(), 0 };
				auto slot = std::make_shared<AssetSlot>();
				slot->Guid = ref.Guid;
				slot->Data = std::make_shared<UiPanelData>(data);
				slot->State = state;
				Slots[ref.Guid] = slot;
				return ref;
			}

			entt::entity add_canvas(const AssetRef& panel, i32 sortOrder = 0)
			{
				const entt::entity entity = TheScene->create("Canvas");
				TheScene->registry().emplace<UICanvas>(entity, UICanvas{ panel, sortOrder, true });
				return entity;
			}

			UiDrawList draw()
			{
				TheScene->update_world_transforms();
				Module.update_interaction(*TheScene, Canvas);
				UiDrawList list;
				Module.build_draw_list(*TheScene, Canvas, list);
				return list;
			}
		};

		UiPanelData one_widget_panel(const UiWidget& widget)
		{
			UiPanelData panel;
			panel.Widgets = { widget };
			panel.NextId = widget.Id + 1;
			return panel;
		}

		void check_module(Check& check, const FontData* font)
		{
			UiWidget red = layout_widget(1, 0, WidgetKind::Panel, "Red", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
			red.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
			UiWidget blue = red;
			blue.Name = "Blue";
			blue.Color = { 0.0f, 0.0f, 1.0f, 1.0f };

			check(Engine::get().get_module<UiModule>() != nullptr, "ui_module_registered: the engine has a UiModule");
			check(UiModule{}.get_priority() < 0 && UiModule{}.get_priority() > -50, /* scripts are 0, the renderer -50 */ "ui_module_order: UI events are dispatched before scripts update, after the scene and the renderer open the frame");

			// a canvas with no panel
			{
				ModuleRig rig(font);
				rig.add_canvas(AssetRef{});
				const UiDrawList list = rig.draw();
				check(list.empty(), "canvas_unset_draws_nothing: a canvas without a panel draws nothing");
				check(rig.Module.instance_count() == 0, "canvas_unset_draws_nothing: and makes no instance");
			}

			// loading, ready, failed
			{
				ModuleRig rig(font);
				const AssetRef panel = rig.add_panel(one_widget_panel(red), LoadState::Loading);
				const entt::entity canvas = rig.add_canvas(panel);
				check(rig.draw().empty(), "canvas_panel_loading_then_ready: while the panel is loading nothing is drawn");
				rig.Slots[panel.Guid]->State = LoadState::Ready;
				const UiDrawList list = rig.draw();
				check(list.Vertices.size() == 4 && near_rect(quad_rect(list, 0), { 0, 0 }, rig.Canvas), "canvas_panel_loading_then_ready: once it is ready it is drawn");
				check(rig.Module.instance_of(canvas) != nullptr && rig.Module.instance_of(canvas)->ready(), "canvas_panel_loading_then_ready: the canvas has a ready instance");

				const AssetRef broken = rig.add_panel(one_widget_panel(red), LoadState::Failed);
				const entt::entity brokenCanvas = rig.add_canvas(broken);
				rig.draw();
				rig.draw();
				const UiPanelInstance* instance = rig.Module.instance_of(brokenCanvas);
				check(instance != nullptr && !instance->ready() && instance->failure_reports() == 1, "canvas_failed_logs_once: a failed panel is reported once, however many frames pass");
				check(rig.draw().Vertices.size() == 4, "canvas_failed_logs_once: and the other canvases still draw");

				// the editor cooks it again: the next ask gets the new asset
				rig.Slots[broken.Guid] = rig.Slots[panel.Guid];
				rig.Module.reload_panel(broken);
				check(rig.draw().Vertices.size() == 8, "canvas_failed_logs_once: a failed panel that is cooked again is drawn");
			}

			// hidden canvas, sort order
			{
				ModuleRig rig(font);
				const AssetRef redPanel = rig.add_panel(one_widget_panel(red));
				const AssetRef bluePanel = rig.add_panel(one_widget_panel(blue));
				const entt::entity top = rig.add_canvas(redPanel, 5);
				rig.add_canvas(bluePanel, 1);
				UiDrawList list = rig.draw();
				check(list.Vertices.size() == 8 && list.Vertices[0].Color == pack_color(0, 0, 1, 1) && list.Vertices[4].Color == pack_color(1, 0, 0, 1), "canvas_sort_order: a higher sort order is drawn later");

				rig.TheScene->registry().get<UICanvas>(top).Visible = false;
				list = rig.draw();
				check(list.Vertices.size() == 4 && list.Vertices[0].Color == pack_color(0, 0, 1, 1), "canvas_invisible: a canvas that is not visible draws nothing");
				rig.TheScene->registry().get<UICanvas>(top).Visible = true;
				rig.Module.instance_of(top)->set_visible(false);
				check(rig.draw().Vertices.size() == 4, "canvas_invisible: neither does a panel that a script hid");

				rig.Canvas = { 0.0f, 0.0f };
				check(rig.draw().empty(), "canvas_zero_viewport: a viewport without area draws nothing");
				rig.Canvas = { 1920.0f, 0.0f };
				check(rig.draw().empty(), "canvas_zero_viewport: neither does one without height");
			}

			// state belongs to the instance
			{
				ModuleRig rig(font);
				UiWidget group = layout_widget(1, 0, WidgetKind::VStack, "Menu", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 300, 300 });
				UiWidget start = layout_widget(2, 1, WidgetKind::Button, "Start", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 100, 30 });
				UiWidget label = layout_widget(3, 2, WidgetKind::Text, "Label", { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 });
				label.Text = "Start";
				UiPanelData data;
				data.Widgets = { group, start, label };
				data.NextId = 4;
				const AssetRef panel = rig.add_panel(data);
				const entt::entity first = rig.add_canvas(panel);
				const entt::entity second = rig.add_canvas(panel);
				rig.draw();

				UiPanelInstance* a = rig.Module.instance_of(first);
				UiPanelInstance* b = rig.Module.instance_of(second);
				check(a != nullptr && b != nullptr && a != b, "instance_state_isolated: every canvas has its own instance");
				if (a != nullptr && b != nullptr)
				{
					check(a->find("Menu/Start/Label") == std::optional<UiId>(3) && a->find("Menu/Start") == std::optional<UiId>(2) && a->find("Menu") == std::optional<UiId>(1), "instance_find_path: widgets are found by the path of their names");
					check(!a->find("Menu/Nope").has_value() && !a->find("").has_value() && !a->find("Nope").has_value() && !a->find("Menu//Start").has_value() && !a->find("/").has_value(), "instance_find_path: a path that names nothing finds nothing");

					a->data().Widgets[2].Text = "Changed";
					check(b->data().Widgets[2].Text == "Start" && std::static_pointer_cast<UiPanelData>(rig.Slots[panel.Guid]->Data)->Widgets[2].Text == "Start", "instance_state_isolated: changing one instance changes neither the other nor the asset");

					// a re-cooked panel keeps what scripts set
					UiPanelData edited = data;
					edited.Widgets[0].Spacing = 12.0f;
					edited.Widgets[2].Text = "From the file";
					auto slot = std::make_shared<AssetSlot>();
					slot->Data = std::make_shared<UiPanelData>(edited);
					slot->State = LoadState::Ready;
					rig.Slots[panel.Guid] = slot;
					rig.Module.reload_panel(panel);
					rig.draw();
					check(a->data().Widgets[0].Spacing == 12.0f && a->data().Widgets[2].Text == "Changed" && b->data().Widgets[2].Text == "From the file", "instance_reload_keeps_state: a re-cooked panel brings the new layout, keeps the text a script set, and takes the edited text where nothing was set");
				}

				// clicking through the module
				rig.Source.move_to({ 50.0f, 15.0f });
				rig.Source.press(0);
				rig.Source.release(0);
				rig.draw();
				check(b != nullptr && std::any_of(b->events().begin(), b->events().end(), [](const UiEvent& event) { return event.Kind == UiEventKind::Click && event.Widget == 2; }), "module_input: input from the source reaches the widgets of a canvas");
				check(a != nullptr && a->events().empty(), "module_input: and the canvas below the one that took the pointer gets nothing");
				rig.draw();
				check(b != nullptr && b->events().empty(), "module_input: events last one frame");
			}

			// scene changes
			{
				ModuleRig rig(font);
				const AssetRef panel = rig.add_panel(one_widget_panel(red));
				rig.add_canvas(panel);
				rig.draw();
				check(rig.Module.instance_count() == 1, "instance_dropped_on_scene_change: a canvas gets an instance");
				const entt::entity canvas = rig.TheScene->registry().view<UICanvas>().front();
				rig.TheScene->registry().remove<UICanvas>(canvas);
				rig.draw();
				check(rig.Module.instance_count() == 0, "instance_dropped_on_scene_change: the instance goes with the component");

				rig.add_canvas(panel);
				rig.draw();
				Scene other;
				UiDrawList list;
				rig.Module.update_interaction(other, rig.Canvas);
				rig.Module.build_draw_list(other, rig.Canvas, list);
				check(rig.Module.instance_count() == 0 && list.empty(), "instance_dropped_on_scene_change: another scene starts without instances");
			}

			// world anchors through the module
			{
				ModuleRig rig(font);
				const entt::entity target = rig.TheScene->create("Target");
				const EntityGuid targetGuid = rig.TheScene->registry().get<Id>(target).Guid;

				UiWidget marker = layout_widget(1, 0, WidgetKind::Panel, "Marker", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 40, 40 });
				marker.WorldEntity = targetGuid;
				marker.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
				const AssetRef panel = rig.add_panel(one_widget_panel(marker));
				rig.add_canvas(panel);

				UiDrawList list = rig.draw();
				check(list.Vertices.size() == 4 && near_rect(quad_rect(list, 0), { 940, 520 }, { 980, 560 }, 1.0f), "anchor_follows_entity: the widget is at the projection of the entity");
				rig.TheScene->registry().get<Transform>(target).Position = { 1.0f, 0.0f, 0.0f };
				list = rig.draw();
				check(list.Vertices.size() == 4 && quad_rect(list, 0).Min.x > 960.0f + 20.0f, "anchor_follows_entity: when the entity moves right the widget moves right");

				rig.TheScene->destroy(target);
				list = rig.draw();
				check(list.empty(), "anchor_entity_destroyed: when the entity is gone the widget is hidden");

				// the editor's camera instead of the scene's
				RenderCamera editor;
				editor.Position = { 0.0f, 0.0f, 50.0f };
				editor.View = glm::lookAt(editor.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				editor.Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
				const entt::entity second = rig.TheScene->create("Second");
				rig.TheScene->registry().get<Transform>(second).Position = { 5.0f, 0.0f, 0.0f };
				const EntityGuid secondGuid = rig.TheScene->registry().get<Id>(second).Guid;
				UiWidget secondMarker = marker;
				secondMarker.WorldEntity = secondGuid;
				const AssetRef panel2 = rig.add_panel(one_widget_panel(secondMarker));
				rig.add_canvas(panel2);
				rig.TheScene->update_world_transforms();
				const float nearX = quad_rect(rig.draw(), 0).Min.x;
				rig.Module.set_camera_override(editor);
				const float farX = quad_rect(rig.draw(), 0).Min.x;
				check(farX < nearX && farX > 960.0f - 21.0f, "module_camera_override: the camera the editor draws with is the one the anchors use (further away is nearer the middle)");
				rig.Module.set_camera_override(std::nullopt);

				rig.TheScene->destroy(rig.Camera);
				check(rig.draw().empty(), "anchor_no_camera: without a camera the anchored widgets are hidden");
			}
		}

		void check_play_input(Check& check)
		{
			// the window pointer becomes a pixel of the viewport image
			{
				EditorPlayInputSource source;
				source.set_viewport({ 100.0f, 50.0f }, 1.0f, true);
				UiPointerSample sample;
				sample.Pointer = { 500.0f, 350.0f };
				sample.OverImage = true;
				UiInputFrame frame = source.convert(sample);
				check(frame.PointerValid && frame.Pointer == glm::vec2(400.0f, 300.0f), "play_input_conversion: the pointer is relative to the image");

				source.set_viewport({ 100.0f, 50.0f }, 2.0f, true);
				sample.Pointer = { 150.0f, 100.0f };
				frame = source.convert(sample);
				check(frame.PointerValid && frame.Pointer == glm::vec2(100.0f, 100.0f), "play_input_hidpi_scale: and in pixels of the image, not points of the window");
			}

			// outside the image
			{
				EditorPlayInputSource source;
				source.set_viewport({ 100.0f, 50.0f }, 1.0f, false);
				UiPointerSample sample;
				sample.Pointer = { 10.0f, 10.0f };
				sample.OverImage = false;
				sample.Down[0] = sample.Pressed[0] = true;
				sample.NavPressed = ui_nav_bit(UiNav::Down);
				sample.Scroll = 3.0f;
				const UiInputFrame frame = source.convert(sample);
				check(!frame.PointerValid && !frame.Pressed[0] && !frame.Down[0] && frame.NavPressed == 0 && frame.Scroll == 0.0f, "play_input_outside_invalid: off the image there is no pointer and nothing is forwarded");
			}

			// a drag that starts on the image goes on off it, and its release is seen
			{
				EditorPlayInputSource source;
				source.set_viewport({ 0.0f, 0.0f }, 1.0f, true);
				UiPointerSample sample;
				sample.Pointer = { 20.0f, 20.0f };
				sample.OverImage = true;
				sample.Down[0] = sample.Pressed[0] = true;
				UiInputFrame frame = source.convert(sample);
				check(frame.Pressed[0] && frame.Down[0] && frame.PointerValid, "play_input_drag: a press on the image is forwarded");

				sample.Pressed[0] = false;
				sample.OverImage = false;
				sample.Pointer = { 900.0f, 20.0f };
				frame = source.convert(sample);
				check(frame.Down[0] && frame.PointerValid && frame.Pointer.x == 900.0f, "play_input_drag: the drag goes on off the image, with the pointer where it is");

				sample.Down[0] = false;
				sample.Released[0] = true;
				frame = source.convert(sample);
				check(frame.Released[0] && !frame.Down[0] && frame.PointerValid, "play_input_drag: the release off the image is forwarded");

				sample.Released[0] = false;
				frame = source.convert(sample);
				check(!frame.PointerValid && !frame.Down[0], "play_input_drag: and afterwards the pointer is off again");
			}

			// a press elsewhere that moves over the image is not a press on it
			{
				EditorPlayInputSource source;
				source.set_viewport({ 0.0f, 0.0f }, 1.0f, true);
				UiPointerSample sample;
				sample.Pointer = { 900.0f, 20.0f };
				sample.OverImage = false;
				sample.Down[0] = sample.Pressed[0] = true;
				source.convert(sample);
				sample.Pressed[0] = false;
				sample.OverImage = true;
				sample.Pointer = { 20.0f, 20.0f };
				const UiInputFrame frame = source.convert(sample);
				check(frame.PointerValid && !frame.Down[0] && !frame.Pressed[0], "play_input_elsewhere: a drag that began off the image does not press what it passes over");
			}

			// the keys, over the image
			{
				EditorPlayInputSource source;
				source.set_viewport({ 0.0f, 0.0f }, 1.0f, true);
				UiPointerSample sample;
				sample.OverImage = true;
				sample.Pointer = { 5.0f, 5.0f };
				sample.NavPressed = ui_nav_bit(UiNav::Up) | ui_nav_bit(UiNav::Submit);
				sample.Scroll = 2.0f;
				const UiInputFrame frame = source.convert(sample);
				check(frame.NavPressed == (ui_nav_bit(UiNav::Up) | ui_nav_bit(UiNav::Submit)) && frame.Scroll == 2.0f, "play_input_keys: navigation and scroll over the image are forwarded");
			}
		}

		UiId add_named(UiEditModel& model, WidgetKind kind, UiId parent, const char* name)
		{
			const UiId id = model.add(kind, parent);
			if (id != 0)
			{
				model.rename(id, name);
			}
			return id;
		}

		std::vector<std::string> child_names(const UiEditModel& model, UiId parent)
		{
			std::vector<std::string> names;
			for (const UiWidget& widget : model.data().Widgets)
			{
				if (widget.Parent == parent)
				{
					names.push_back(widget.Name);
				}
			}
			return names;
		}

		void check_edit_model(Check& check)
		{
			// adding
			{
				UiEditModel model{ UiPanelData{} };
				check(model.data().Widgets.empty() && !model.dirty() && !model.can_undo() && !model.can_redo(), "edit_new_model: nothing in it, nothing to save or undo");

				const UiId root = model.add(WidgetKind::Panel, 0);
				const UiId child = model.add(WidgetKind::Button, root);
				check(root != 0 && child != 0 && root != child && model.data().find(child)->Parent == root && model.data().find(child)->Kind == WidgetKind::Button, "edit_add_child: a widget is added under its parent with a new id");
				check(model.add(WidgetKind::Text, 999) == 0 && model.data().Widgets.size() == 2, "edit_add_child: a parent that does not exist adds nothing");
				check(model.data().NextId > child && model.dirty() && model.can_undo(), "edit_add_child: the panel remembers the next id, and is dirty");

				const UiId second = model.add(WidgetKind::Button, root);
				const UiId third = model.add(WidgetKind::Button, root);
				check(child_names(model, root) == std::vector<std::string>({ "Button", "Button 1", "Button 2" }) && second != third, "edit_unique_names: siblings of one name get a number");
				model.add(WidgetKind::Button, 0);
				check(child_names(model, 0) == std::vector<std::string>({ "Panel", "Button" }), "edit_unique_names: and another parent starts again");

				for (const WidgetKind kind : { WidgetKind::Panel, WidgetKind::HStack, WidgetKind::VStack, WidgetKind::Text, WidgetKind::Image, WidgetKind::Button, WidgetKind::Slider, WidgetKind::Toggle, WidgetKind::Bar })
				{
					const UiId id = model.add(kind, 0);
					const UiWidget* widget = model.data().find(id);
					const glm::vec2 size = widget != nullptr ? widget->OffsetMax - widget->OffsetMin : glm::vec2(0.0f);
					check(widget != nullptr && widget->Kind == kind && size.x > 0.0f && size.y > 0.0f, std::string("edit_add_defaults: a new ") + widget_kind_name(kind) + " has a size you can see");
				}
			}

			// names
			{
				UiEditModel model{ UiPanelData{} };
				const UiId a = add_named(model, WidgetKind::Panel, 0, "A");
				const UiId b = add_named(model, WidgetKind::Panel, 0, "B");
				check(!model.rename(a, "") && !model.rename(a, "x/y") && !model.rename(a, "B") && !model.rename(999, "Z"), "edit_rename_rules: an empty name, a path separator, a taken name and a widget that does not exist are refused");
				check(model.rename(a, "A") && model.rename(b, "C") && model.data().find(b)->Name == "C", "edit_rename_rules: its own name is fine, and a free one is taken");
			}

			// moving
			{
				UiEditModel model{ UiPanelData{} };
				const UiId a = add_named(model, WidgetKind::Panel, 0, "A");
				const UiId b = add_named(model, WidgetKind::Panel, a, "B");
				const UiId c = add_named(model, WidgetKind::Panel, b, "C");
				check(!model.reparent(a, c, 0) && !model.reparent(a, b, 0) && !model.reparent(a, a, 0), "edit_reparent_cycle_refused: a widget cannot go under itself or its descendants");
				check(model.data().find(a)->Parent == 0, "edit_reparent_cycle_refused: and nothing changed");
				check(!model.reparent(c, 999, 0) && !model.reparent(999, a, 0), "edit_reparent_cycle_refused: neither under or of a widget that does not exist");

				const UiId d = add_named(model, WidgetKind::Panel, 0, "D");
				const UiId e = add_named(model, WidgetKind::Panel, 0, "E");
				check(model.reparent(e, a, 0) && model.data().find(e)->Parent == a, "edit_reparent: a widget moves to another parent");
				check(child_names(model, a) == std::vector<std::string>({ "E", "B" }), "edit_reparent: and takes the position it was dropped at");
				check(model.reparent(d, a, 99) && child_names(model, a) == std::vector<std::string>({ "E", "B", "D" }), "edit_reparent: a position past the end is the end");
				check(model.reparent(b, a, 0) && child_names(model, a) == std::vector<std::string>({ "B", "E", "D" }) && model.data().find(c)->Parent == b, "edit_reparent: within one parent it reorders, and the children go along");

				const UiId clash = add_named(model, WidgetKind::Panel, 0, "E");
				check(model.reparent(clash, a, 0) && child_names(model, a) == std::vector<std::string>({ "E 1", "B", "E", "D" }), "edit_reparent: a name that is taken in the new parent is made unique");
			}

			// removing and duplicating
			{
				UiEditModel model{ UiPanelData{} };
				const UiId root = add_named(model, WidgetKind::VStack, 0, "Root");
				const UiId button = add_named(model, WidgetKind::Button, root, "Go");
				const UiId label = add_named(model, WidgetKind::Text, button, "Label");
				const UiId other = add_named(model, WidgetKind::Panel, 0, "Other");
				model.set_field(label, "Text", FieldValue(std::string("Hello")));

				const UiId copy = model.duplicate(root);
				check(copy != 0 && copy != root && model.data().Widgets.size() == 7, "edit_duplicate_subtree_new_ids: the copy has as many widgets as the original");
				std::set<UiId> ids;
				for (const UiWidget& widget : model.data().Widgets)
				{
					ids.insert(widget.Id);
				}
				check(ids.size() == model.data().Widgets.size(), "edit_duplicate_subtree_new_ids: and every widget its own id");
				check(model.data().find(copy)->Name == "Root 1" && model.data().find(copy)->Parent == 0, "edit_duplicate_subtree_new_ids: the copy is beside the original with a free name");
				UiId copiedLabel = 0;
				for (const UiWidget& widget : model.data().Widgets)
				{
					if (widget.Name == "Label" && widget.Id != label)
					{
						copiedLabel = widget.Id;
					}
				}
				check(copiedLabel != 0 && model.data().find(copiedLabel)->Text == "Hello" && model.data().find(copiedLabel)->Parent != button, "edit_duplicate_subtree_new_ids: the copy keeps the fields and hangs under the copied parent");
				check(child_names(model, 0) == std::vector<std::string>({ "Root", "Root 1", "Other" }), "edit_duplicate_subtree_new_ids: the copy comes right after the original");

				check(model.remove(root) && model.data().find(root) == nullptr && model.data().find(button) == nullptr && model.data().find(label) == nullptr && model.data().find(other) != nullptr, "edit_remove_subtree: a widget goes with everything under it");
				check(!model.remove(root) && !model.remove(0) && model.duplicate(999) == 0, "edit_remove_subtree: a widget that is not there cannot be removed or copied");
			}

			// fields
			{
				UiEditModel model{ UiPanelData{} };
				const UiId id = add_named(model, WidgetKind::Slider, 0, "Volume");
				const size_t before = model.data().Widgets.size();
				check(model.set_field(id, "MaxValue", FieldValue(10.0f)) && model.data().find(id)->MaxValue == 10.0f, "edit_set_field: a value of the right type is set");
				check(!model.set_field(id, "MaxValue", FieldValue(std::string("ten"))) && model.data().find(id)->MaxValue == 10.0f, "edit_set_field_type_mismatch: a value of another type changes nothing");
				check(!model.set_field(id, "Nope", FieldValue(1.0f)) && !model.set_field(999, "MaxValue", FieldValue(1.0f)), "edit_set_field_type_mismatch: neither does a field or a widget that does not exist");
				check(!model.set_field(id, "Align", FieldValue(static_cast<i32>(7))), "edit_set_field_type_mismatch: nor an enum value that is not one of its names");
				check(model.set_field(id, "Name", FieldValue(std::string("Music"))) && model.data().find(id)->Name == "Music" && !model.set_field(id, "Name", FieldValue(std::string("a/b"))), "edit_set_field: Name goes through the rename rules");
				check(model.data().Widgets.size() == before, "edit_set_field: no widget came or went");

				UiEditModel fresh{ UiPanelData{} };
				const UiId same = fresh.add(WidgetKind::Slider, 0);
				const bool setSame = fresh.set_field(same, "MaxValue", FieldValue(fresh.data().find(same)->MaxValue));
				fresh.undo(); // undoes the add: setting the value it already had was not a step
				check(setSame && fresh.data().find(same) == nullptr, "edit_set_field: setting the value it has is not a step of its own");

				check(model.set_rect(id, { 0, 0 }, { 1, 0 }, { 10, 20 }, { -10, 60 }), "edit_set_rect_valid: a rect is set");
				const UiWidget* widget = model.data().find(id);
				check(widget->AnchorMin == glm::vec2(0, 0) && widget->AnchorMax == glm::vec2(1, 0) && widget->OffsetMin == glm::vec2(10, 20) && widget->OffsetMax == glm::vec2(-10, 60), "edit_set_rect_valid: all four edges");
				check(!model.set_rect(id, { 0, 0 }, { std::numeric_limits<float>::quiet_NaN(), 0 }, { 0, 0 }, { 1, 1 }) && !model.set_rect(999, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }), "edit_set_rect_valid: NaN and a widget that is not there are refused");
				check(!model.set_reference({ 0.0f, 1080.0f }, 0.5f) && !model.set_reference({ 1920.0f, 1080.0f }, 2.0f) && model.set_reference({ 1280.0f, 720.0f }, 1.0f) && model.data().ReferenceResolution == glm::vec2(1280.0f, 720.0f), "edit_set_reference: a reference resolution needs an area and a match between 0 and 1");
			}

			// undo, redo and the dirty flag
			{
				UiEditModel model{ UiPanelData{} };
				const UiPanelData original = model.data();
				const UiId root = add_named(model, WidgetKind::Panel, 0, "Root");
				check(model.data().find(root) != nullptr, "edit_undo_redo: a widget was added and named");
				model.rename(root, "Renamed");
				model.remove(root);
				check(model.data().Widgets.empty(), "edit_undo_redo: and then renamed and removed");

				model.undo();
				check(model.data().find(root) != nullptr && model.data().find(root)->Name == "Renamed", "edit_undo_redo: undo brings the widget back");
				model.undo();
				model.undo();
				model.undo();
				check(model.data().Widgets.empty() && !model.can_undo() && model.data().ReferenceResolution == original.ReferenceResolution, "edit_undo_redo: all the undos are back at the start");
				model.undo();
				check(model.data().Widgets.empty(), "edit_undo_redo: undo with nothing to undo does nothing");
				model.redo();
				model.redo();
				check(model.data().find(root) != nullptr && model.data().find(root)->Name == "Root", "edit_undo_redo: redo adds it again");
				model.redo();
				model.redo();
				model.redo();
				check(model.data().Widgets.empty() && !model.can_redo(), "edit_undo_redo: and goes forward to the end");

				model.undo();
				model.add(WidgetKind::Text, 0);
				check(!model.can_redo(), "edit_undo_redo: a new change ends the redo history");

				// ids are not reused, even after an undo
				UiEditModel ids{ UiPanelData{} };
				const UiId first = ids.add(WidgetKind::Panel, 0);
				ids.undo();
				const UiId again = ids.add(WidgetKind::Panel, 0);
				check(again != first, "edit_ids_not_reused: an id that was undone is not handed out again");
			}

			{
				UiEditModel model{ UiPanelData{} };
				check(!model.dirty(), "edit_dirty_flag: a fresh model is clean");
				const UiId id = model.add(WidgetKind::Panel, 0);
				check(model.dirty(), "edit_dirty_flag: a change makes it dirty");
				model.mark_saved();
				check(!model.dirty(), "edit_dirty_flag: saving makes it clean");
				model.rename(id, "Moved");
				check(model.dirty(), "edit_dirty_flag: and the next change dirty again");
				model.undo();
				check(!model.dirty(), "edit_dirty_flag: undoing back to what was saved is clean again");
				model.undo();
				check(model.dirty(), "edit_dirty_flag: undoing past it is dirty");
				model.redo();
				check(!model.dirty(), "edit_dirty_flag: and redoing to it is clean");
			}

			{
				UiEditModel model{ UiPanelData{} };
				for (int i = 0; i < 130; i++)
				{
					model.add(WidgetKind::Panel, 0);
				}
				int undone = 0;
				while (model.can_undo() && undone < 500)
				{
					model.undo();
					undone++;
				}
				check(undone == 100 && model.data().Widgets.size() == 30, "edit_history_cap: only the latest 100 changes can be undone");
			}

			// what the editor saves is what the importer reads
			{
				UiEditModel model{ UiPanelData{} };
				const UiId root = add_named(model, WidgetKind::VStack, 0, "Menu");
				const UiId button = add_named(model, WidgetKind::Button, root, "Start");
				model.set_field(button, "HoverColor", FieldValue(glm::vec4(0.1f, 0.2f, 0.3f, 1.0f)));
				model.set_field(add_named(model, WidgetKind::Text, button, "Label"), "Text", FieldValue(std::string("Play \xC3\xA9")));
				model.set_reference({ 1280.0f, 720.0f }, 0.25f);

				std::string error;
				const std::optional<UiPanelData> back = ui_panel_from_text(ui_panel_to_json(model.data()).dump(2), error);
				check(back && panels_equal(model.data(), *back), "edit_save_roundtrip: the model's data survives the file");
			}
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

		bool near_vec4(const glm::vec4& a, const glm::vec4& b, float tolerance = 0.01f) { return glm::length(a - b) <= tolerance; }

		bool finite_vec4(const glm::vec4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }

		void check_video_widget(Check& check)
		{
			// the kind and its fields
			check(std::string(widget_kind_name(WidgetKind::Video)) == "Video", "video_kind_name: the kind is called Video");
			check(widget_kind_from_name("Video") == WidgetKind::Video, "video_kind_name: a Video is found by name");
			check(ui_field_visible(WidgetKind::Video, "Video") && ui_field_visible(WidgetKind::Video, "Fit") && ui_field_visible(WidgetKind::Video, "Loop")
				&& ui_field_visible(WidgetKind::Video, "AutoPlay") && ui_field_visible(WidgetKind::Video, "Volume") && ui_field_visible(WidgetKind::Video, "LetterboxColor")
				&& ui_field_visible(WidgetKind::Video, "Color"), "video_fields_visible: a Video shows its fields");
			check(!ui_field_visible(WidgetKind::Text, "Video") && !ui_field_visible(WidgetKind::Bar, "LetterboxColor") && !ui_field_visible(WidgetKind::Video, "Text"), "video_fields_visible: other kinds do not show them, and a Video does not show text");
			const UiField* fit = find_ui_field("Fit");
			check(fit != nullptr && fit->Type == FieldType::Enum, "video_fields: Fit is an enum field");
			const UiWidget defaults;
			check(defaults.Fit == UiVideoFit::Letterbox && !defaults.Loop && defaults.AutoPlay && defaults.Volume == 1.0f && defaults.LetterboxColor == glm::vec4(0, 0, 0, 1), "video_defaults: letterbox, no loop, auto play, full volume, black bars");

			// fit math
			const glm::vec4 square{ 0.0f, 0.0f, 1000.0f, 1000.0f };
			const glm::vec2 wide{ 1920.0f, 1080.0f };
			UiVideoRects r = ui_video_fit(square, wide, UiVideoFit::Letterbox);
			check(near_vec4(r.Picture, { 0.0f, 218.75f, 1000.0f, 781.25f }) && near_vec4(r.Uv, { 0, 0, 1, 1 }), "video_fit_letterbox: 16:9 in a square is a centred band with the whole picture");
			check(near_vec4(r.BarA, { 0.0f, 0.0f, 1000.0f, 218.75f }) && near_vec4(r.BarB, { 0.0f, 781.25f, 1000.0f, 1000.0f }), "video_fit_letterbox: the bars are the rest of the rect");
			r = ui_video_fit({ 0.0f, 0.0f, 2000.0f, 500.0f }, wide, UiVideoFit::Letterbox);
			check(near_vec4(r.Picture, { 555.5556f, 0.0f, 1444.4444f, 500.0f }, 0.1f) && r.BarA.z - r.BarA.x > 0.0f && r.BarB.z - r.BarB.x > 0.0f, "video_fit_letterbox: a wide rect gets bars at the sides");
			r = ui_video_fit(square, wide, UiVideoFit::Cover);
			check(near_vec4(r.Picture, square) && near_vec4(r.Uv, { 0.21875f, 0.0f, 0.78125f, 1.0f }), "video_fit_cover: the picture fills the rect and the uv crops the sides");
			check(r.BarA.z - r.BarA.x <= 0.0f && r.BarB.z - r.BarB.x <= 0.0f || (r.BarA == glm::vec4(0.0f) && r.BarB == glm::vec4(0.0f)), "video_fit_cover: no bars");
			r = ui_video_fit(square, wide, UiVideoFit::Stretch);
			check(near_vec4(r.Picture, square) && near_vec4(r.Uv, { 0, 0, 1, 1 }) && r.BarA == glm::vec4(0.0f) && r.BarB == glm::vec4(0.0f), "video_fit_stretch: the whole picture over the whole rect");
			bool allFinite = true;
			const float bad[] = { 0.0f, -5.0f, std::nanf(""), std::numeric_limits<float>::infinity(), 1e30f };
			for (const UiVideoFit mode : { UiVideoFit::Stretch, UiVideoFit::Letterbox, UiVideoFit::Cover })
			{
				for (const float v : bad)
				{
					for (const glm::vec2 size : { glm::vec2(v, 1080.0f), glm::vec2(1920.0f, v), glm::vec2(v, v) })
					{
						for (const glm::vec4 rect : { square, glm::vec4(0, 0, 0, 0), glm::vec4(10, 10, 5, 5), glm::vec4(0, 0, v, v) })
						{
							const UiVideoRects out = ui_video_fit(rect, size, mode);
							allFinite = allFinite && finite_vec4(out.Picture) && finite_vec4(out.Uv) && finite_vec4(out.BarA) && finite_vec4(out.BarB);
						}
					}
				}
			}
			check(allFinite, "video_fit_degenerate: a video or rect of size 0, negative, NaN, infinite or huge gives finite rects");

			// drawing
			const glm::vec2 canvas{ 1920.0f, 1080.0f };
			UiLayoutInput input;
			input.CanvasSize = canvas;
			constexpr u32 TEXTURE = 1u << 23;
			bool hasView = true;
			input.Video = [&](UiId id) -> std::optional<UiVideoView>
			{
				return id == 1 && hasView ? std::optional<UiVideoView>(UiVideoView{ TEXTURE, wide }) : std::nullopt;
			};
			const auto build = [&](UiWidget widget)
			{
				UiPanelData panel;
				panel.Widgets = { widget };
				const UiLayoutResult layout = compute_layout(panel, input);
				UiDrawList list;
				append_panel(list, panel, std::vector<UiInteractionState>(1, UiInteractionState::Normal), layout, input);
				return list;
			};
			UiWidget widget = layout_widget(1, 0, WidgetKind::Video, "Intro", { 0, 0 }, { 0, 0 }, { 0, 0 }, { 1000, 1000 });
			widget.LetterboxColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			UiDrawList list = build(widget);
			u32 imageBatches = 0;
			u32 solidBatches = 0;
			for (const UiBatch& batch : list.Batches)
			{
				imageBatches += batch.Kind == UiQuadKind::Image && batch.ViewTexture == TEXTURE ? 1 : 0;
				solidBatches += batch.Kind == UiQuadKind::Solid ? 1 : 0;
			}
			check(imageBatches == 1 && solidBatches >= 1 && list.Vertices.size() == 12, "video_draw: a letterboxed video is the picture and two bars");
			widget.Fit = UiVideoFit::Stretch;
			list = build(widget);
			check(list.Vertices.size() == 4 && list.Batches.size() == 1 && list.Batches[0].ViewTexture == TEXTURE, "video_draw_stretch: a stretched video is one picture quad");
			widget.Fit = UiVideoFit::Letterbox;
			hasView = false;
			list = build(widget);
			check(list.Vertices.size() == 4 && list.Batches.size() == 1 && list.Batches[0].Kind == UiQuadKind::Solid && list.Batches[0].ViewTexture == 0, "video_draw_no_picture: with no picture the rect is the bar colour");
			widget.LetterboxColor.a = 0.0f;
			check(build(widget).empty(), "video_draw_no_picture: a clear bar colour draws nothing");
			widget.LetterboxColor.a = 1.0f;
			input.Video = nullptr;
			check(build(widget).Vertices.size() == 4, "video_draw_no_module: without a video source the widget is still its bar colour");
			widget.Visible = false;
			check(build(widget).empty(), "video_draw: a hidden video draws nothing");
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

	std::filesystem::path write_ui_editor_smoke_panel()
	{
		UiEditModel model{ UiPanelData{} };
		const UiId root = model.add(WidgetKind::Panel, 0);
		model.set_field(root, "Name", FieldValue(std::string("Root")));
		model.set_rect(root, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f });
		model.set_field(root, "Color", FieldValue(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)));

		const UiId menu = model.add(WidgetKind::VStack, root);
		model.set_field(menu, "Name", FieldValue(std::string("Menu")));
		const UiId title = model.add(WidgetKind::Text, menu);
		model.set_field(title, "Name", FieldValue(std::string("Title")));
		model.set_field(title, "Text", FieldValue(std::string("Hello, UI")));
		model.set_field(title, "Outline", FieldValue(0.2f));
		model.set_field(title, "Shadow", FieldValue(true));
		model.add(WidgetKind::Button, menu);
		model.add(WidgetKind::Slider, menu);
		model.add(WidgetKind::Toggle, menu);

		const UiId bar = model.add(WidgetKind::Bar, root);
		model.set_field(bar, "Name", FieldValue(std::string("Floating")));
		model.set_field(bar, "WorldEntity", FieldValue(EntityGuid::generate())); // shown at its stand-in position

		if (AssetManager* assets = Engine::get().get_module<AssetManager>())
		{
			if (const std::optional<AssetRef> font = assets->find_asset("Assets/fonts/Lato-Regular.ttf"))
			{
				model.set_field(title, "Font", FieldValue(*font));
			}
		}

		const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wiwa_ui_editor_smoke";
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		const std::filesystem::path file = directory / "smoke.wuipanel";
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << ui_panel_to_json(model.data()).dump(2);
		return file;
	}

	bool run_ui_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_field_table(check);
		check_video_widget(check);
		check_panel_files(check);
		Handle<FontData> fontHandle;
		check_fonts(check, pipeline, fontHandle);
		check_layout(check, fontHandle.get());
		check_draw(check, fontHandle.get());
		check_interaction(check);
		check_module(check, fontHandle.get());
		check_play_input(check);
		check_edit_model(check);

		if (check.Failures == 0)
		{
			WI_UI_LOG(Info, "UI smoke test passed");
		}
		return check.Failures == 0;
	}
}
