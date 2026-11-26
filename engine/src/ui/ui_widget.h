#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>
#include <scene/entity_guid.h>
#include <scene/reflection/field_descriptor.h>

// The data of a UI panel: a tree of widgets stored flat. A widget's fields are described once, by ui_fields(), which the panel
// files, the UI editor's inspector and the Lua bindings all read.
namespace wiwa
{
	// Stable inside a panel, never reused by the editor. 0 means "no widget" (the parent of a root widget).
	using UiId = u32;

	constexpr u32 UI_MAX_WIDGETS = 1u << 16; // a panel with more is corrupt or hostile

	enum class WidgetKind : u32
	{
		Panel,
		HStack,
		VStack,
		Text,
		Image,
		Button,
		Slider,
		Toggle,
		Bar,
		ViewImage, // an offscreen view (a ViewHost of the scene), by name
		Video,     // a playing video (an mp4 asset), see video/video_module.h

		Last
	};

	enum class UiVideoFit : u32 { Stretch, Letterbox, Cover }; // how a video's picture fills the widget's rect
	enum class UiTextAlign : u32 { Left, Center, Right };
	enum class UiDirection : u32 { LeftToRight, RightToLeft, BottomToTop, TopToBottom };
	enum class UiChildAlign : u32 { Start, Center, End };
	enum class UiOffscreen : u32 { Hide, ClampToEdge }; // what a world anchored widget does when its point is outside the canvas
	// What the pointer or the keyboard is doing to a widget; the draw list tints buttons by it.
	enum class UiInteractionState : u32 { Normal, Hover, Pressed, Focused };

	const char* widget_kind_name(WidgetKind kind);
	std::optional<WidgetKind> widget_kind_from_name(std::string_view name);

	// One widget of any kind: the fields that a kind does not use (see ui_field_visible) keep their defaults.
	struct UiWidget
	{
		// Structure: not fields, the editor changes them through the document model.
		UiId Id = 0;
		UiId Parent = 0;
		WidgetKind Kind = WidgetKind::Panel;

		std::string Name{}; // unique among siblings; scripts find widgets by the path of names

		// Rect, relative to the parent: anchors are fractions of the parent's rect, offsets are pixels (at the reference resolution)
		// from the anchor points.
		glm::vec2 AnchorMin{ 0.0f };
		glm::vec2 AnchorMax{ 0.0f };
		glm::vec2 Pivot{ 0.5f };
		glm::vec2 OffsetMin{ 0.0f };
		glm::vec2 OffsetMax{ 100.0f, 30.0f };

		bool Visible = true;
		bool Enabled = true;
		float Opacity = 1.0f;
		glm::vec4 Color{ 1.0f };
		glm::vec4 BackgroundColor{ 0.0f, 0.0f, 0.0f, 0.5f }; // the track of a slider or a bar, the box of a toggle
		bool ClipChildren = false;

		// Text
		std::string Text{};
		AssetRef Font{};
		float FontSize = 24.0f;
		UiTextAlign Align = UiTextAlign::Left;
		bool Wrap = false;
		float Outline = 0.0f; // 0..1 of the distance range
		glm::vec4 OutlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		bool Shadow = false;

		// Image (and the picture of a panel or button)
		AssetRef Texture{};
		bool Slice9 = false;
		glm::vec4 SliceBorders{ 0.0f }; // left, top, right, bottom in texels
		glm::vec4 UvRect{ 0.0f, 0.0f, 1.0f, 1.0f }; // the part of the texture that is shown: u0, v0, u1, v1 (0..1, v down, like the texture's rows). A sprite sheet cell; with Slice9 the borders are cut from this part

		// ViewImage: the Name of the ViewHost whose picture this shows
		std::string ViewName{};

		// Video: the file, how the picture fits the rect, and what happens on its own. Color tints the picture; LetterboxColor fills the bars (and
		// the whole rect while nothing is playing).
		AssetRef Video{};
		UiVideoFit Fit = UiVideoFit::Letterbox;
		bool Loop = false;
		bool AutoPlay = true;
		float Volume = 1.0f; // 0..1
		glm::vec4 LetterboxColor{ 0.0f, 0.0f, 0.0f, 1.0f };

		// Slider, toggle (Value > 0.5 is on) and bar (0..1)
		float Value = 0.0f;
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		float Step = 0.0f; // 0 is continuous
		UiDirection Direction = UiDirection::LeftToRight;

		// Stacks
		float Spacing = 0.0f;
		glm::vec4 Padding{ 0.0f }; // left, top, right, bottom
		UiChildAlign ChildAlign = UiChildAlign::Start;
		bool ForceExpand = false;

		// Button
		glm::vec4 HoverColor{ 0.85f, 0.85f, 0.85f, 1.0f };
		glm::vec4 PressedColor{ 0.7f, 0.7f, 0.7f, 1.0f };
		glm::vec4 DisabledColor{ 0.5f, 0.5f, 0.5f, 0.5f };

		// World anchor: with WorldEntity set, the widget's pivot sits on the screen position of the entity (plus offsets).
		EntityGuid WorldEntity{};
		glm::vec3 WorldOffset{ 0.0f };
		glm::vec2 WorldScreenOffset{ 0.0f };
		bool WorldScaleWithDistance = false;
		float WorldRefDistance = 10.0f;
		float WorldMinScale = 0.25f;
		float WorldMaxScale = 2.0f;
		float WorldFadeStart = 0.0f; // distances; fading is off when WorldFadeEnd <= WorldFadeStart
		float WorldFadeEnd = 0.0f;
		bool WorldHideBehind = true;
		UiOffscreen WorldOffscreen = UiOffscreen::Hide;

		bool world_anchored() const { return WorldEntity.valid(); }
	};

	// A panel asset: the widgets of one screen or one floating element. Widgets are in tree order: a widget's siblings keep the
	// order they have in the array, and a parent may come after its children.
	struct UiPanelData
	{
		glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
		float MatchWidthOrHeight = 0.5f; // 0 scales with the width, 1 with the height
		std::vector<UiWidget> Widgets{};
		UiId NextId = 1;

		// The widget with this id, or null.
		UiWidget* find(UiId id);
		const UiWidget* find(UiId id) const;
	};

	// A field of a widget: the descriptor the inspector needs, and how to read and write it on a widget.
	struct UiField : FieldDescriptor
	{
		std::function<FieldValue(const UiWidget&)> Get{};
		// False, and nothing changes, if the value is not valid for the field.
		std::function<bool(UiWidget&, const FieldValue&)> Set{};
	};

	// Every field, in the order the files and the inspector use.
	const std::vector<UiField>& ui_fields();
	const UiField* find_ui_field(std::string_view name);

	// Whether the editor shows the field for widgets of this kind (the rest are not used by the kind).
	bool ui_field_visible(WidgetKind kind, std::string_view field);
}
