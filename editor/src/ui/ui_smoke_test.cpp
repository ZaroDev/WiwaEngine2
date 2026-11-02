#include <editor_common.h>
#include "ui_smoke_test.h"

#include <assets/wasset_io.h>
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
		(void)pipeline;
		Check check;

		check_field_table(check);
		check_panel_files(check);

		if (check.Failures == 0)
		{
			WI_UI_LOG(Info, "UI smoke test passed");
		}
		return check.Failures == 0;
	}
}
