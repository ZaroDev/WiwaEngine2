#include <pch.h>
#include "ui_widget.h"

#include <algorithm>
#include <memory>
#include <type_traits>

#include <scene/reflection/reflect_macros.h>

namespace wiwa
{
	namespace
	{
		constexpr const char* KIND_NAMES[] = { "Panel", "HStack", "VStack", "Text", "Image", "Button", "Slider", "Toggle", "Bar", "ViewImage", "Video" };
		static_assert(std::size(KIND_NAMES) == static_cast<size_t>(WidgetKind::Last));

		template<typename Accessor>
		void add_field(std::vector<UiField>& fields, const char* name, Accessor access, FieldMeta meta = {})
		{
			using M = std::remove_cvref_t<std::invoke_result_t<Accessor, UiWidget&>>;
			const UiWidget defaults{};

			UiField field;
			field.Name = name;
			field.Type = FieldTypeOf<M>::Value;
			if (field.Type == FieldType::Vec4 && meta.IsColor)
			{
				field.Type = FieldType::Color;
			}
			field.Meta = std::move(meta);
			field.Default = detail::to_field_value<M>(access(defaults));
			field.Get = [access](const UiWidget& widget) { return detail::to_field_value<M>(access(widget)); };

			const FieldDescriptor descriptor = field; // the setter checks against the type and the enum names, not against the functions
			field.Set = [access, descriptor](UiWidget& widget, const FieldValue& value)
			{
				if (!is_valid_field_value(descriptor, value))
				{
					return false;
				}
				detail::from_field_value<M>(access(widget), value);
				return true;
			};
			fields.push_back(std::move(field));
		}

		FieldMeta color_meta()
		{
			return FieldMeta::color();
		}

		FieldMeta unit_range()
		{
			return FieldMeta::range(0.0f, 1.0f);
		}

#define UI_FIELD(Member) add_field(fields, #Member, [](auto& widget) -> auto& { return widget.Member; })
#define UI_FIELD_META(Member, Meta) add_field(fields, #Member, [](auto& widget) -> auto& { return widget.Member; }, Meta)

		std::vector<UiField> build_fields()
		{
			std::vector<UiField> fields;

			UI_FIELD(Name);
			UI_FIELD(AnchorMin);
			UI_FIELD(AnchorMax);
			UI_FIELD(Pivot);
			UI_FIELD(OffsetMin);
			UI_FIELD(OffsetMax);
			UI_FIELD(Visible);
			UI_FIELD(Enabled);
			UI_FIELD_META(Opacity, unit_range());
			UI_FIELD_META(Color, color_meta());
			UI_FIELD_META(BackgroundColor, color_meta());
			UI_FIELD(ClipChildren);

			UI_FIELD(Text);
			UI_FIELD_META(Font, FieldMeta::asset(AssetType::Font));
			UI_FIELD(FontSize);
			UI_FIELD_META(Align, FieldMeta::enumeration({ "Left", "Center", "Right" }));
			UI_FIELD(Wrap);
			UI_FIELD_META(Outline, unit_range());
			UI_FIELD_META(OutlineColor, color_meta());
			UI_FIELD(Shadow);

			UI_FIELD_META(Texture, FieldMeta::asset(AssetType::Texture));
			UI_FIELD(Slice9);
			UI_FIELD(SliceBorders);
			UI_FIELD(ViewName);

			UI_FIELD_META(Video, FieldMeta::asset(AssetType::Video));
			UI_FIELD_META(Fit, FieldMeta::enumeration({ "Stretch", "Letterbox", "Cover" }));
			UI_FIELD(Loop);
			UI_FIELD(AutoPlay);
			UI_FIELD_META(Volume, unit_range());
			UI_FIELD_META(LetterboxColor, color_meta());

			UI_FIELD(Value);
			UI_FIELD(MinValue);
			UI_FIELD(MaxValue);
			UI_FIELD(Step);
			UI_FIELD_META(Direction, FieldMeta::enumeration({ "LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom" }));

			UI_FIELD(Spacing);
			UI_FIELD(Padding);
			UI_FIELD_META(ChildAlign, FieldMeta::enumeration({ "Start", "Center", "End" }));
			UI_FIELD(ForceExpand);

			UI_FIELD_META(HoverColor, color_meta());
			UI_FIELD_META(PressedColor, color_meta());
			UI_FIELD_META(DisabledColor, color_meta());

			UI_FIELD(WorldEntity);
			UI_FIELD(WorldOffset);
			UI_FIELD(WorldScreenOffset);
			UI_FIELD(WorldScaleWithDistance);
			UI_FIELD(WorldRefDistance);
			UI_FIELD(WorldMinScale);
			UI_FIELD(WorldMaxScale);
			UI_FIELD(WorldFadeStart);
			UI_FIELD(WorldFadeEnd);
			UI_FIELD(WorldHideBehind);
			UI_FIELD_META(WorldOffscreen, FieldMeta::enumeration({ "Hide", "ClampToEdge" }));
			return fields;
		}

#undef UI_FIELD
#undef UI_FIELD_META

		// The fields that only some kinds use; every other field is shown for every kind.
		struct KindFields
		{
			WidgetKind Kind;
			std::vector<std::string_view> Names;
		};

		const std::vector<std::string_view>& all_kind_specific()
		{
			static const std::vector<std::string_view> names = {
				"Color", "BackgroundColor", "ClipChildren",
				"Text", "Font", "FontSize", "Align", "Wrap", "Outline", "OutlineColor", "Shadow",
				"Texture", "Slice9", "SliceBorders", "ViewName",
				"Video", "Fit", "Loop", "AutoPlay", "Volume", "LetterboxColor",
				"Value", "MinValue", "MaxValue", "Step", "Direction",
				"Spacing", "Padding", "ChildAlign", "ForceExpand",
				"HoverColor", "PressedColor", "DisabledColor" };
			return names;
		}

		const std::vector<KindFields>& kind_fields()
		{
			static const std::vector<KindFields> table = {
				{ WidgetKind::Panel, { "Color", "Texture", "ClipChildren" } },
				{ WidgetKind::HStack, { "Color", "Spacing", "Padding", "ChildAlign", "ForceExpand", "ClipChildren" } },
				{ WidgetKind::VStack, { "Color", "Spacing", "Padding", "ChildAlign", "ForceExpand", "ClipChildren" } },
				{ WidgetKind::Text, { "Color", "Text", "Font", "FontSize", "Align", "Wrap", "Outline", "OutlineColor", "Shadow" } },
				{ WidgetKind::Image, { "Color", "Texture", "Slice9", "SliceBorders" } },
				{ WidgetKind::Button, { "Color", "HoverColor", "PressedColor", "DisabledColor", "Texture", "ClipChildren" } },
				{ WidgetKind::Slider, { "Color", "BackgroundColor", "Value", "MinValue", "MaxValue", "Step", "Direction" } },
				{ WidgetKind::Toggle, { "Color", "BackgroundColor", "Value" } },
				{ WidgetKind::Bar, { "Color", "BackgroundColor", "Value", "Direction" } },
				{ WidgetKind::ViewImage, { "Color", "ViewName" } },
				{ WidgetKind::Video, { "Color", "Video", "Fit", "Loop", "AutoPlay", "Volume", "LetterboxColor" } },
			};
			return table;
		}
	}

	const char* widget_kind_name(WidgetKind kind)
	{
		const size_t index = static_cast<size_t>(kind);
		return index < std::size(KIND_NAMES) ? KIND_NAMES[index] : "?";
	}

	std::optional<WidgetKind> widget_kind_from_name(std::string_view name)
	{
		for (size_t i = 0; i < std::size(KIND_NAMES); i++)
		{
			if (name == KIND_NAMES[i])
			{
				return static_cast<WidgetKind>(i);
			}
		}
		return std::nullopt;
	}

	UiWidget* UiPanelData::find(UiId id)
	{
		for (UiWidget& widget : Widgets)
		{
			if (widget.Id == id)
			{
				return &widget;
			}
		}
		return nullptr;
	}

	const UiWidget* UiPanelData::find(UiId id) const
	{
		return const_cast<UiPanelData*>(this)->find(id);
	}

	const std::vector<UiField>& ui_fields()
	{
		static const std::vector<UiField> fields = build_fields();
		return fields;
	}

	const UiField* find_ui_field(std::string_view name)
	{
		for (const UiField& field : ui_fields())
		{
			if (field.Name == name)
			{
				return &field;
			}
		}
		return nullptr;
	}

	bool ui_field_visible(WidgetKind kind, std::string_view field)
	{
		const std::vector<std::string_view>& specific = all_kind_specific();
		if (std::find(specific.begin(), specific.end(), field) == specific.end())
		{
			return true; // the name, the rect, Visible, Enabled, Opacity and the world anchor belong to every kind
		}

		for (const KindFields& entry : kind_fields())
		{
			if (entry.Kind == kind)
			{
				return std::find(entry.Names.begin(), entry.Names.end(), field) != entry.Names.end();
			}
		}
		return false;
	}
}
