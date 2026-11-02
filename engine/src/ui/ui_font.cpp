#include <pch.h>
#include "ui_font.h"

#include <algorithm>
#include <limits>

namespace wiwa
{
	namespace
	{
		constexpr u32 REPLACEMENT = 0xFFFD;
		constexpr u32 TAB_SPACES = 4;

		struct Placed
		{
			const FontGlyph* Glyph = nullptr; // null: nothing to draw (the font has no glyph, not even the fallback)
			float X = 0.0f;                   // of the pen, from the start of the line
			float Advance = 0.0f;
			bool Space = false;
		};

		struct Line
		{
			std::vector<Placed> Glyphs{};
			float Width = 0.0f; // without the spaces at the end
		};

		const FontGlyph* resolve(const FontData& font, u32 codepoint)
		{
			if (const FontGlyph* glyph = font.find(codepoint))
			{
				return glyph;
			}
			return font.find(font.FallbackCodepoint);
		}

		// Drops the spaces at the end and measures what is left.
		void close_line(Line& line)
		{
			while (!line.Glyphs.empty() && line.Glyphs.back().Space)
			{
				line.Glyphs.pop_back();
			}
			line.Width = line.Glyphs.empty() ? 0.0f : line.Glyphs.back().X + line.Glyphs.back().Advance;
		}

		std::vector<Line> build_lines(const FontData& font, const std::vector<u32>& codepoints, float scale, float maxWidth, bool wrap)
		{
			std::vector<Line> lines;
			if (codepoints.empty())
			{
				return lines;
			}

			const bool limited = wrap && maxWidth > 0.0f;
			Line current;
			float pen = 0.0f;          // where the next glyph goes
			size_t wordStart = 0;      // index in current.Glyphs of the word being placed
			float wordStartPen = 0.0f;
			u32 previous = 0;

			const auto finish = [&]()
			{
				close_line(current);
				lines.push_back(std::move(current));
				current = Line{};
				pen = 0.0f;
				wordStart = 0;
				wordStartPen = 0.0f;
				previous = 0;
			};

			for (const u32 codepoint : codepoints)
			{
				if (codepoint == '\r')
				{
					continue;
				}
				if (codepoint == '\n')
				{
					finish();
					continue;
				}

				const bool space = codepoint == ' ' || codepoint == '\t';
				const FontGlyph* glyph = resolve(font, space ? static_cast<u32>(' ') : codepoint);
				float advance = glyph != nullptr ? glyph->Advance * scale : 0.0f;
				if (codepoint == '\t')
				{
					advance *= static_cast<float>(TAB_SPACES);
				}
				const float kern = previous != 0 ? font.kerning(previous, codepoint) * scale : 0.0f;

				if (limited && !space && !current.Glyphs.empty() && pen + kern + advance > maxWidth)
				{
					if (wordStart > 0)
					{
						// The word moves to a line of its own; what was in front of it stays.
						Line moved;
						const float shift = wordStartPen;
						for (size_t i = wordStart; i < current.Glyphs.size(); i++)
						{
							Placed placed = current.Glyphs[i];
							placed.X -= shift;
							moved.Glyphs.push_back(placed);
						}
						current.Glyphs.resize(wordStart);
						const float movedPen = pen - shift;
						close_line(current);
						lines.push_back(std::move(current));
						current = std::move(moved);
						pen = movedPen;
						wordStart = 0;
						wordStartPen = 0.0f;

						if (pen + kern + advance > maxWidth && !current.Glyphs.empty())
						{
							finish(); // the word alone is wider than the box: break between its characters
						}
					}
					else
					{
						finish();
					}
				}

				const float x = current.Glyphs.empty() ? 0.0f : pen + kern;
				current.Glyphs.push_back(Placed{ glyph, x, advance, space });
				pen = x + advance;
				previous = codepoint;
				if (space)
				{
					wordStart = current.Glyphs.size();
					wordStartPen = pen;
				}
			}

			finish();
			return lines;
		}

		float line_height(const FontData& font, float size)
		{
			return font.LineHeight * (size / font.PixelSize);
		}
	}

	std::vector<u32> decode_utf8(std::string_view text)
	{
		std::vector<u32> result;
		result.reserve(text.size());

		size_t i = 0;
		while (i < text.size())
		{
			const u8 lead = static_cast<u8>(text[i]);
			if (lead < 0x80)
			{
				result.push_back(lead);
				i++;
				continue;
			}

			u32 length = 0;
			u32 value = 0;
			u32 minimum = 0;
			if ((lead & 0xE0) == 0xC0) { length = 2; value = lead & 0x1F; minimum = 0x80; }
			else if ((lead & 0xF0) == 0xE0) { length = 3; value = lead & 0x0F; minimum = 0x800; }
			else if ((lead & 0xF8) == 0xF0) { length = 4; value = lead & 0x07; minimum = 0x10000; }

			bool valid = length != 0 && i + length <= text.size();
			for (u32 k = 1; valid && k < length; k++)
			{
				const u8 next = static_cast<u8>(text[i + k]);
				valid = (next & 0xC0) == 0x80;
				value = (value << 6) | (next & 0x3F);
			}
			valid = valid && value >= minimum && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);

			if (valid)
			{
				result.push_back(value);
				i += length;
			}
			else
			{
				result.push_back(REPLACEMENT);
				i++;
			}
		}
		return result;
	}

	glm::vec2 measure_text(const FontData& font, std::string_view utf8, float size, float maxWidth, bool wrap)
	{
		if (font.PixelSize <= 0.0f || size <= 0.0f)
		{
			return glm::vec2(0.0f);
		}

		const float scale = size / font.PixelSize;
		const std::vector<Line> lines = build_lines(font, decode_utf8(utf8), scale, maxWidth, wrap);
		float width = 0.0f;
		for (const Line& line : lines)
		{
			width = std::max(width, line.Width);
		}
		return glm::vec2(width, static_cast<float>(lines.size()) * line_height(font, size));
	}

	std::vector<TextGlyphQuad> layout_text(const FontData& font, std::string_view utf8, float size, const glm::vec2& boxSize, UiTextAlign align, bool wrap)
	{
		std::vector<TextGlyphQuad> quads;
		if (font.PixelSize <= 0.0f || size <= 0.0f)
		{
			return quads;
		}

		const float scale = size / font.PixelSize;
		const std::vector<Line> lines = build_lines(font, decode_utf8(utf8), scale, boxSize.x, wrap);

		float boxWidth = boxSize.x;
		if (!(boxWidth > 0.0f))
		{
			for (const Line& line : lines)
			{
				boxWidth = std::max(boxWidth, line.Width);
			}
		}

		const float height = line_height(font, size);
		for (size_t row = 0; row < lines.size(); row++)
		{
			const Line& line = lines[row];
			float start = 0.0f;
			if (align == UiTextAlign::Center)
			{
				start = (boxWidth - line.Width) * 0.5f;
			}
			else if (align == UiTextAlign::Right)
			{
				start = boxWidth - line.Width;
			}

			const float baseline = static_cast<float>(row) * height + font.Ascent * scale;
			for (const Placed& placed : line.Glyphs)
			{
				if (placed.Glyph == nullptr || placed.Glyph->Size.x <= 0.0f || placed.Glyph->Size.y <= 0.0f)
				{
					continue;
				}

				TextGlyphQuad quad;
				quad.Min = glm::vec2(start + placed.X, baseline) + placed.Glyph->Bearing * scale;
				quad.Max = quad.Min + placed.Glyph->Size * scale;
				quad.UvMin = placed.Glyph->UvMin;
				quad.UvMax = placed.Glyph->UvMax;
				quads.push_back(quad);
			}
		}
		return quads;
	}
}
