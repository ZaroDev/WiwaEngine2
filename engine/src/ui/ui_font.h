#pragma once

#include <string_view>
#include <vector>

#include <assets/asset_data.h>
#include <math/math.h>

#include "ui_widget.h"

// Text for the UI: which glyph quads a string makes in a font, and how big the string is. Pure functions, no GPU.
namespace wiwa
{
	// One glyph to draw, relative to the top left of the text box, in pixels.
	struct TextGlyphQuad
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };
		glm::vec2 UvMin{ 0.0f };
		glm::vec2 UvMax{ 0.0f };
	};

	// The code points of UTF-8 text. A byte that does not belong to a valid sequence becomes U+FFFD, so any bytes decode.
	std::vector<u32> decode_utf8(std::string_view text);

	// The extent of the text at this size (the height of a line is the font's line height). maxWidth only matters with wrap and above 0:
	// words that do not fit move to the next line, and a word wider than maxWidth breaks between its characters. '\n' starts a line.
	// An empty string is 0 by 0.
	glm::vec2 measure_text(const FontData& font, std::string_view utf8, float size, float maxWidth, bool wrap);

	// The quads of the visible glyphs (a space makes none) in a box: lines wrap at the width of the box when wrap is set, each line is
	// aligned inside the box width (or inside the widest line when the box has no width), and the first line's top is the box top.
	// A character the font lacks draws the font's fallback glyph.
	std::vector<TextGlyphQuad> layout_text(const FontData& font, std::string_view utf8, float size, const glm::vec2& boxSize, UiTextAlign align, bool wrap);
}
