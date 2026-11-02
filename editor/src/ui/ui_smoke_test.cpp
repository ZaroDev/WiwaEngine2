#include <editor_common.h>
#include "ui_smoke_test.h"

#include "../asset_pipeline/asset_database.h"

#include <algorithm>
#include <cmath>

#include <assets/asset_manager.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <ui/ui_font.h>
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

		void check_fonts(Check& check, AssetPipelineModule& pipeline)
		{
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();

			const std::optional<AssetRef> ref = database.ensure_imported(database.get_assets_root() / "fonts" / "Lato-Regular.ttf");
			check(ref.has_value(), "font_imported: the shipped font imports");
			if (!ref)
			{
				return;
			}
			Handle<FontData> handle = manager.load<FontData>(*ref);
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
		check_fonts(check, pipeline);

		if (check.Failures == 0)
		{
			WI_UI_LOG(Info, "UI smoke test passed");
		}
		return check.Failures == 0;
	}
}
