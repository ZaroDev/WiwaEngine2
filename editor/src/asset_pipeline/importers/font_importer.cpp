#include <editor_common.h>
#include "importers.h"

#include <algorithm>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 MAX_ATLAS_SIDE = 4096;
		constexpr unsigned char EDGE_VALUE = 128; // the texel value on the outline

		struct RenderedGlyph
		{
			u32 Codepoint = 0;
			float Advance = 0.0f;
			int Width = 0;
			int Height = 0;
			int OffsetX = 0;
			int OffsetY = 0;
			std::vector<u8> Pixels{};
			int AtlasX = 0;
			int AtlasY = 0;
		};

		// Shelf packing: glyphs, tallest first, go left to right in rows. Returns the height used, or 0 if they do not fit.
		u32 pack_glyphs(std::vector<RenderedGlyph*>& order, u32 width)
		{
			u32 x = 0;
			u32 y = 0;
			u32 rowHeight = 0;
			for (RenderedGlyph* glyph : order)
			{
				if (glyph->Width <= 0)
				{
					continue;
				}
				const u32 w = static_cast<u32>(glyph->Width);
				const u32 h = static_cast<u32>(glyph->Height);
				if (w > width)
				{
					return 0;
				}
				if (x + w > width)
				{
					y += rowHeight + 1;
					x = 0;
					rowHeight = 0;
				}
				glyph->AtlasX = static_cast<int>(x);
				glyph->AtlasY = static_cast<int>(y);
				x += w + 1;
				rowHeight = std::max(rowHeight, h);
				if (y + rowHeight > MAX_ATLAS_SIDE)
				{
					return 0;
				}
			}
			return std::max(y + rowHeight, 1u);
		}

		// .ttf and .otf: cooked into a FontData, a signed distance field atlas of the characters in the import settings.
		class FontImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "FontImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".ttf", ".otf" }; }

			virtual nlohmann::json default_settings() const override
			{
				// pixelSize: the size the glyphs are rendered at (bigger is sharper when scaled up); sdfRange: pixels of distance the
				// field reaches on each side of the outline (bigger allows thicker outlines and shadows); charset: "ascii" or "latin1"
				return nlohmann::json{ { "pixelSize", 48 }, { "sdfRange", 8 }, { "charset", "latin1" } };
			}

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				const nlohmann::json& settings = ctx.settings();
				const int pixelSize = settings.value("pixelSize", 48);
				const int range = settings.value("sdfRange", 8);
				const std::string charset = settings.value("charset", std::string("latin1"));
				if (pixelSize < 8 || pixelSize > 256 || range < 1 || range > 32 || (charset != "ascii" && charset != "latin1"))
				{
					WI_ASSET_LOG(Error, "{0}: the import settings are not valid (pixelSize 8 to 256, sdfRange 1 to 32, charset ascii or latin1)", file);
					return false;
				}

				const unsigned char* data = reinterpret_cast<const unsigned char*>(bytes->data());
				stbtt_fontinfo info;
				const int offset = stbtt_GetFontOffsetForIndex(data, 0);
				if (offset < 0 || stbtt_InitFont(&info, data, offset) == 0)
				{
					WI_ASSET_LOG(Error, "{0}: not a font file stb_truetype can read", file);
					return false;
				}

				std::vector<u32> codepoints;
				for (u32 cp = 32; cp <= 126; cp++)
				{
					codepoints.push_back(cp);
				}
				if (charset == "latin1")
				{
					for (u32 cp = 160; cp <= 255; cp++)
					{
						codepoints.push_back(cp);
					}
				}

				const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(pixelSize));
				std::vector<RenderedGlyph> glyphs;
				for (const u32 codepoint : codepoints)
				{
					if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0)
					{
						continue;
					}

					RenderedGlyph glyph;
					glyph.Codepoint = codepoint;
					int advance = 0;
					int bearing = 0;
					stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &bearing);
					glyph.Advance = static_cast<float>(advance) * scale;

					unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, static_cast<int>(codepoint), range, EDGE_VALUE,
						static_cast<float>(EDGE_VALUE) / static_cast<float>(range), &glyph.Width, &glyph.Height, &glyph.OffsetX, &glyph.OffsetY);
					if (sdf != nullptr)
					{
						glyph.Pixels.assign(sdf, sdf + static_cast<size_t>(glyph.Width) * glyph.Height);
						stbtt_FreeSDF(sdf, nullptr);
					}
					else
					{
						glyph.Width = 0;
						glyph.Height = 0;
					}
					glyphs.push_back(std::move(glyph));
				}
				if (glyphs.empty())
				{
					WI_ASSET_LOG(Error, "{0}: the font has none of the characters of the charset", file);
					return false;
				}

				std::vector<RenderedGlyph*> order;
				for (RenderedGlyph& glyph : glyphs)
				{
					order.push_back(&glyph);
				}
				std::stable_sort(order.begin(), order.end(), [](const RenderedGlyph* a, const RenderedGlyph* b) { return a->Height > b->Height; });

				u32 atlasWidth = 0;
				u32 atlasHeight = 0;
				for (u32 width = 256; width <= MAX_ATLAS_SIDE && atlasHeight == 0; width *= 2)
				{
					atlasHeight = pack_glyphs(order, width);
					atlasWidth = atlasHeight != 0 ? width : 0;
				}
				if (atlasHeight == 0)
				{
					WI_ASSET_LOG(Error, "{0}: the characters do not fit in a {1}x{1} atlas at pixelSize {2}; use a smaller pixelSize", file, MAX_ATLAS_SIDE, pixelSize);
					return false;
				}

				FontData font;
				font.Name = ctx.source().stem().string();
				font.AtlasWidth = atlasWidth;
				font.AtlasHeight = atlasHeight;
				font.Atlas.assign(static_cast<size_t>(atlasWidth) * atlasHeight, 0);
				font.PixelSize = static_cast<float>(pixelSize);
				font.SdfRange = static_cast<float>(range);

				int ascent = 0;
				int descent = 0;
				int lineGap = 0;
				stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
				font.Ascent = static_cast<float>(ascent) * scale;
				font.Descent = static_cast<float>(descent) * scale;
				font.LineHeight = static_cast<float>(ascent - descent + lineGap) * scale;

				for (const RenderedGlyph& glyph : glyphs) // ascending by codepoint, as they were made
				{
					for (int row = 0; row < glyph.Height; row++)
					{
						std::copy_n(glyph.Pixels.begin() + static_cast<size_t>(row) * glyph.Width, glyph.Width,
							font.Atlas.begin() + static_cast<size_t>(glyph.AtlasY + row) * atlasWidth + glyph.AtlasX);
					}

					FontGlyph out;
					out.Codepoint = glyph.Codepoint;
					out.Advance = glyph.Advance;
					if (glyph.Width > 0)
					{
						out.Size = glm::vec2(static_cast<float>(glyph.Width), static_cast<float>(glyph.Height));
						out.Bearing = glm::vec2(static_cast<float>(glyph.OffsetX), static_cast<float>(glyph.OffsetY));
						out.UvMin = glm::vec2(static_cast<float>(glyph.AtlasX) / static_cast<float>(atlasWidth), static_cast<float>(glyph.AtlasY) / static_cast<float>(atlasHeight));
						out.UvMax = glm::vec2(static_cast<float>(glyph.AtlasX + glyph.Width) / static_cast<float>(atlasWidth), static_cast<float>(glyph.AtlasY + glyph.Height) / static_cast<float>(atlasHeight));
					}
					font.Glyphs.push_back(out);
				}

				font.FallbackCodepoint = font.find('?') != nullptr ? static_cast<u32>('?') : 0;

				for (const FontGlyph& first : font.Glyphs)
				{
					for (const FontGlyph& second : font.Glyphs)
					{
						const int kern = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(first.Codepoint), static_cast<int>(second.Codepoint));
						if (kern != 0)
						{
							font.Kerning.push_back(FontKerning{ first.Codepoint, second.Codepoint, static_cast<float>(kern) * scale });
						}
					}
				}

				ctx.emit(ctx.main_asset(), font);
				return true;
			}
		};
	}

	Scope<IImporter> create_font_importer() { return CreateScope<FontImporter>(); }
}
