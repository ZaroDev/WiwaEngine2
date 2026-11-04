#pragma once

#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>

#include "ui_layout.h"

// What the main thread hands to the render thread to draw the UI: a flat list of textured quads. Plain data, no pointers into the panel.
namespace wiwa
{
	enum class UiQuadKind : u32
	{
		Solid, // a colour; the texture is not used
		Image, // the texture times the vertex colour
		Sdf,   // the red channel of a font atlas, read as a signed distance field, in the vertex colour
	};

	struct UiVertex
	{
		glm::vec2 Pos{ 0.0f }; // canvas pixels
		glm::vec2 Uv{ 0.0f };
		u32 Color = 0;         // RGBA8, red in the lowest byte
	};

	// A run of indices drawn with the same state. Quads that are next to each other with equal state share a batch.
	struct UiBatch
	{
		UiQuadKind Kind = UiQuadKind::Solid;
		AssetRef Texture{}; // the texture of an Image, the font of an Sdf
		glm::vec4 ClipRect{ 0.0f }; // min x, min y, max x, max y in canvas pixels
		u32 FirstIndex = 0;
		u32 IndexCount = 0;
		// Sdf only
		float SdfRange = 0.0f;   // pixels of the atlas the distance field reaches on each side of the outline
		float PixelScale = 1.0f; // screen pixels per atlas pixel
		float Outline = 0.0f;
		u32 OutlineColor = 0;
		bool Shadow = false;
	};

	struct UiDrawList
	{
		std::vector<UiVertex> Vertices{};
		std::vector<u32> Indices{};
		std::vector<UiBatch> Batches{};
		glm::vec2 CanvasSize{ 0.0f };

		bool empty() const { return Indices.empty(); }
	};

	// RGBA8 of a colour with components in 0..1 (outside it is clamped).
	u32 ui_pack_color(const glm::vec4& color);

	// A widget to draw (or to hit test), with the rect that clips it: the canvas, cut down by the clipping widgets above it.
	struct UiDrawItem
	{
		size_t Index = 0;
		glm::vec4 Clip{ 0.0f }; // min x, min y, max x, max y in canvas pixels
	};

	// The widgets that take part, back to front: the ones fixed to the screen in tree order, then the world anchored ones far to near
	// (each with its subtree in tree order). Hidden widgets and widgets with an opacity of 0 are left out together with their children.
	std::vector<UiDrawItem> ui_draw_order(const UiPanelData& panel, const UiLayoutResult& layout, const glm::vec2& canvasSize);

	// Adds the quads of a panel to the list: the widgets that are fixed to the screen in tree order, then the ones anchored in the world,
	// far to near. interaction has the state of each widget by index (missing entries are Normal). Hidden widgets, widgets with an
	// opacity of 0 and quads without an area make nothing. A Text whose font is not loaded (input.Fonts gives null) makes nothing.
	void append_panel(UiDrawList& list, const UiPanelData& panel, const std::vector<UiInteractionState>& interaction, const UiLayoutResult& layout, const UiLayoutInput& input);
}
