#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <assets/asset_data.h>
#include <math/math.h>
#include <scene/entity_guid.h>

#include "ui_widget.h"

// Where every widget of a panel goes on the canvas. Pure CPU work on the main thread.
namespace wiwa
{
	// A rectangle in canvas pixels, y pointing down, Min at the top left.
	struct UiRect
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };

		glm::vec2 size() const { return Max - Min; }
		bool contains(const glm::vec2& point) const { return point.x >= Min.x && point.y >= Min.y && point.x < Max.x && point.y < Max.y; }
	};

	// The camera the world anchors are projected with.
	struct UiCameraView
	{
		glm::mat4 ViewProjection{ 1.0f }; // world to clip space, y pointing up in NDC (a glm::perspective * glm::lookAt)
		glm::vec3 Position{ 0.0f };       // of the camera, in world space (distances for scaling and fading)
		bool Valid = false;
	};

	struct UiLayoutInput
	{
		glm::vec2 CanvasSize{ 0.0f }; // pixels
		const UiCameraView* Camera = nullptr; // null: widgets with a world anchor are hidden
		// The world position of an entity of the active scene, or nothing if it does not exist.
		std::function<std::optional<glm::vec3>(const EntityGuid&)> WorldPosition{};
		// The font of a Text widget, or null while it is not loaded (the text then has no size).
		std::function<const FontData*(const AssetRef&)> Fonts{};
		// The size in texels of a texture, or nothing while it is not loaded (a 9-slice Image is then drawn as a plain quad).
		std::function<std::optional<glm::vec2>(const AssetRef&)> TextureSize{};
		// The texture id (renderer/view/view_types.h: view_texture_id) of the offscreen view with this name, or 0 when there is none (a ViewImage then draws nothing).
		std::function<u32(std::string_view)> ViewTexture{};
	};

	// Per widget, by its index in UiPanelData::Widgets.
	struct UiLayoutResult
	{
		std::vector<UiRect> Rects{};
		std::vector<u8> Visible{};     // the widget and all its parents are visible and, for a world anchor, it is on the screen
		std::vector<float> Opacity{};  // multiplied down the tree, and by the distance fade of a world anchor
		std::vector<float> Scale{};    // pixels per reference pixel at this widget: the canvas scale times the distance scales above it
		std::vector<float> Distance{}; // from the camera to the anchored entity, for a visible world anchored widget; else 0
		float CanvasScale = 0.0f;
		glm::vec2 CanvasSize{ 0.0f };
	};

	// How the canvas is scaled to the reference resolution of the panel: width^(1-match) * height^match of the ratios.
	float ui_canvas_scale(const UiPanelData& panel, const glm::vec2& canvasSize);

	// Rects are the anchors of the parent (fractions of its rect) plus the offsets (reference pixels, scaled). Stacks place their visible
	// children themselves, along their axis, using the size the child asks for (its offsets; a Text without a size is as big as its
	// content). A widget with a world anchor is put with its pivot on the projected point instead, its size scaled by distance if asked.
	// Safe for any input: a canvas without area hides everything, and nothing in the result is NaN.
	UiLayoutResult compute_layout(const UiPanelData& panel, const UiLayoutInput& input);
}
