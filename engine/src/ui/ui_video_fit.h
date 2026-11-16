#pragma once

#include <math/math.h>

#include "ui_widget.h"

namespace wiwa
{
	// Where the picture of a video goes in a rect, and what is left over. All rects are min x, min y, max x, max y in canvas pixels; a rect with no
	// area is unused.
	struct UiVideoRects
	{
		glm::vec4 Picture{ 0.0f }; // where the picture is drawn
		glm::vec4 Uv{ 0.0f, 0.0f, 1.0f, 1.0f }; // the part of the picture that is drawn (Cover crops)
		glm::vec4 BarA{ 0.0f };    // Letterbox: the bar before the picture (top, or left)
		glm::vec4 BarB{ 0.0f };    // the bar after it (bottom, or right)
	};

	// Stretch: the whole picture over the whole rect. Letterbox: the whole picture, as large as fits, centred, bars around it. Cover: the picture
	// fills the rect and the overflow is cropped through the uv. Safe for any input: a picture of no size (0, negative, NaN, infinite) or a rect
	// with no area is a stretch, and nothing in the result is NaN.
	UiVideoRects ui_video_fit(const glm::vec4& rect, glm::vec2 videoSize, UiVideoFit fit);
}
