#include <pch.h>
#include "ui_draw_list.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ui_font.h"
#include "ui_video_fit.h"

namespace wiwa
{
	namespace
	{
		struct SdfParameters
		{
			float Range = 0.0f;
			float PixelScale = 1.0f;
			float Outline = 0.0f;
			u32 OutlineColor = 0;
			bool Shadow = false;
		};

		float finite_or(float value, float fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		float unit(float value)
		{
			return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
		}

		glm::vec4 with_opacity(glm::vec4 color, float opacity)
		{
			color.a *= opacity;
			return color;
		}

		bool clips_children(const UiWidget& widget)
		{
			const bool container = widget.Kind == WidgetKind::Panel || widget.Kind == WidgetKind::HStack || widget.Kind == WidgetKind::VStack || widget.Kind == WidgetKind::Button;
			return container && widget.ClipChildren;
		}

		class Builder
		{
		public:
			Builder(UiDrawList& list, const UiPanelData& panel, const std::vector<UiInteractionState>& interaction, const UiLayoutResult& layout, const UiLayoutInput& input)
				: m_List(list), m_Panel(panel), m_Interaction(interaction), m_Layout(layout), m_Input(input)
			{
			}

			void build()
			{
				for (const UiDrawItem& item : ui_draw_order(m_Panel, m_Layout, m_Input.CanvasSize))
				{
					draw_widget(item.Index, item.Clip);
				}
			}

		private:
			UiInteractionState state_of(size_t i) const
			{
				return i < m_Interaction.size() ? m_Interaction[i] : UiInteractionState::Normal;
			}

			void draw_widget(size_t i, const glm::vec4& clip)
			{
				const UiWidget& widget = m_Panel.Widgets[i];
				const UiRect& rect = m_Layout.Rects[i];
				const float opacity = m_Layout.Opacity[i];
				const UiInteractionState state = state_of(i);

				switch (widget.Kind)
				{
				case WidgetKind::Panel:
				case WidgetKind::HStack:
				case WidgetKind::VStack:
					fill(rect, with_opacity(widget.Color, opacity), widget.Kind == WidgetKind::Panel ? widget.Texture : AssetRef{}, clip, widget.UvRect);
					break;

				case WidgetKind::Image:
					draw_image(i, rect, with_opacity(widget.Color, opacity), clip);
					break;

				case WidgetKind::ViewImage:
					draw_view_image(widget, rect, with_opacity(widget.Color, opacity), clip);
					break;

				case WidgetKind::Video:
					draw_video(widget, rect, opacity, clip);
					break;

				case WidgetKind::Button:
				{
					glm::vec4 color = widget.Color;
					if (!widget.Enabled)
					{
						color = widget.DisabledColor;
					}
					else if (state == UiInteractionState::Pressed)
					{
						color = widget.PressedColor;
					}
					else if (state == UiInteractionState::Hover || state == UiInteractionState::Focused)
					{
						color = widget.HoverColor;
					}
					fill(rect, with_opacity(color, opacity), widget.Texture, clip, widget.UvRect);
					break;
				}

				case WidgetKind::Bar:
				{
					fill(rect, with_opacity(widget.BackgroundColor, opacity), AssetRef{}, clip);
					fill(fill_part(rect, unit(widget.Value), widget.Direction), with_opacity(widget.Color, opacity), AssetRef{}, clip);
					break;
				}

				case WidgetKind::Slider:
					draw_slider(widget, rect, opacity, state, clip);
					break;

				case WidgetKind::Toggle:
				{
					fill(rect, with_opacity(widget.BackgroundColor, opacity), AssetRef{}, clip);
					if (widget.Value > 0.5f)
					{
						const glm::vec2 size = rect.size();
						const float inset = std::min(size.x, size.y) * 0.2f;
						const glm::vec4 color = widget.Enabled ? widget.Color : widget.DisabledColor;
						fill(UiRect{ rect.Min + glm::vec2(inset), rect.Max - glm::vec2(inset) }, with_opacity(color, opacity), AssetRef{}, clip);
					}
					break;
				}

				case WidgetKind::Text:
					draw_text(i, rect, opacity, clip);
					break;

				default:
					break;
				}
			}

			// the part of a rect a bar or slider has filled, from the side the direction starts at
			static UiRect fill_part(const UiRect& rect, float t, UiDirection direction)
			{
				UiRect part = rect;
				const glm::vec2 size = rect.size();
				switch (direction)
				{
				case UiDirection::LeftToRight: part.Max.x = rect.Min.x + size.x * t; break;
				case UiDirection::RightToLeft: part.Min.x = rect.Max.x - size.x * t; break;
				case UiDirection::BottomToTop: part.Min.y = rect.Max.y - size.y * t; break;
				case UiDirection::TopToBottom: part.Max.y = rect.Min.y + size.y * t; break;
				}
				return part;
			}

			void draw_slider(const UiWidget& widget, const UiRect& rect, float opacity, UiInteractionState state, const glm::vec4& clip)
			{
				const float range = widget.MaxValue - widget.MinValue;
				const float t = std::isfinite(range) && range > 0.0f && std::isfinite(widget.Value) ? unit((widget.Value - widget.MinValue) / range) : 0.0f;

				fill(rect, with_opacity(widget.BackgroundColor, opacity), AssetRef{}, clip);
				fill(fill_part(rect, t, widget.Direction), with_opacity(widget.Enabled ? widget.Color : widget.DisabledColor, opacity), AssetRef{}, clip);

				const bool horizontal = widget.Direction == UiDirection::LeftToRight || widget.Direction == UiDirection::RightToLeft;
				const glm::vec2 size = rect.size();
				const float thickness = horizontal ? size.y : size.x;
				const float length = horizontal ? size.x : size.y;
				const bool reversed = widget.Direction == UiDirection::RightToLeft || widget.Direction == UiDirection::BottomToTop;
				float along = (reversed ? 1.0f - t : t) * length;
				along = std::clamp(along, thickness * 0.5f, std::max(length - thickness * 0.5f, thickness * 0.5f));

				UiRect handle;
				if (horizontal)
				{
					handle.Min = glm::vec2(rect.Min.x + along - thickness * 0.5f, rect.Min.y);
					handle.Max = glm::vec2(handle.Min.x + thickness, rect.Max.y);
				}
				else
				{
					handle.Min = glm::vec2(rect.Min.x, rect.Min.y + along - thickness * 0.5f);
					handle.Max = glm::vec2(rect.Max.x, handle.Min.y + thickness);
				}

				glm::vec4 color(1.0f);
				if (!widget.Enabled)
				{
					color = widget.DisabledColor;
				}
				else if (state == UiInteractionState::Pressed)
				{
					color = widget.PressedColor;
				}
				else if (state == UiInteractionState::Hover || state == UiInteractionState::Focused)
				{
					color = widget.HoverColor;
				}
				fill(handle, with_opacity(color, opacity), AssetRef{}, clip);
			}

			// The picture of an offscreen view, stretched over the rect. A view that does not exist (or is not known to this list) draws nothing.
			void draw_view_image(const UiWidget& widget, const UiRect& rect, const glm::vec4& color, const glm::vec4& clip)
			{
				if (!m_Input.ViewTexture || widget.ViewName.empty())
				{
					return;
				}
				const u32 texture = m_Input.ViewTexture(widget.ViewName);
				if (texture == 0 || !(color.a > 0.0f))
				{
					return;
				}
				m_ViewTexture = texture;
				quad(UiQuadKind::Image, AssetRef{}, clip, SdfParameters{}, rect, glm::vec2(0.0f), glm::vec2(1.0f), color);
				m_ViewTexture = 0;
			}

			// The picture that is playing, fitted into the rect, with the bar colour around it; with no picture (not loaded yet, stopped before the
			// first frame, no video module) the whole rect in the bar colour. The UI editor has no player: it shows that.
			void draw_video(const UiWidget& widget, const UiRect& rect, float opacity, const glm::vec4& clip)
			{
				const glm::vec4 bars = with_opacity(widget.LetterboxColor, opacity);
				std::optional<UiVideoView> view;
				if (m_Input.Video)
				{
					view = m_Input.Video(widget.Id);
				}
				if (!view || view->Texture == 0 || !(view->Size.x > 0.0f) || !(view->Size.y > 0.0f))
				{
					fill(rect, bars, AssetRef{}, clip);
					return;
				}

				const UiVideoRects fit = ui_video_fit(glm::vec4(rect.Min, rect.Max), view->Size, widget.Fit);
				fill(UiRect{ { fit.BarA.x, fit.BarA.y }, { fit.BarA.z, fit.BarA.w } }, bars, AssetRef{}, clip);
				fill(UiRect{ { fit.BarB.x, fit.BarB.y }, { fit.BarB.z, fit.BarB.w } }, bars, AssetRef{}, clip);

				const glm::vec4 tint = with_opacity(widget.Color, opacity);
				if (!(tint.a > 0.0f))
				{
					return;
				}
				m_ViewTexture = view->Texture;
				quad(UiQuadKind::Image, AssetRef{}, clip, SdfParameters{}, UiRect{ { fit.Picture.x, fit.Picture.y }, { fit.Picture.z, fit.Picture.w } },
					glm::vec2(fit.Uv.x, fit.Uv.y), glm::vec2(fit.Uv.z, fit.Uv.w), tint);
				m_ViewTexture = 0;
			}

			void draw_image(size_t i, const UiRect& rect, const glm::vec4& color, const glm::vec4& clip)
			{
				const UiWidget& widget = m_Panel.Widgets[i];
				std::optional<glm::vec2> textureSize;
				if (widget.Slice9 && widget.Texture.valid() && m_Input.TextureSize)
				{
					textureSize = m_Input.TextureSize(widget.Texture);
				}
				if (!textureSize || !(textureSize->x > 0.0f) || !(textureSize->y > 0.0f))
				{
					fill(rect, color, widget.Texture, clip, widget.UvRect);
					return;
				}

				// columns and rows: pixels on the canvas, and the same cut points in UV. The borders are texels of the shown part (UvRect), not of the whole texture.
				const glm::vec4 uv = widget.UvRect;
				const glm::vec2 region = *textureSize * glm::vec2(std::abs(uv.z - uv.x), std::abs(uv.w - uv.y));
				if (!(region.x > 0.0f) || !(region.y > 0.0f))
				{
					return;
				}
				const float scale = m_Layout.Scale[i];
				const glm::vec2 size = rect.size();
				float left = std::max(widget.SliceBorders.x, 0.0f);
				float top = std::max(widget.SliceBorders.y, 0.0f);
				float right = std::max(widget.SliceBorders.z, 0.0f);
				float bottom = std::max(widget.SliceBorders.w, 0.0f);
				const auto fit = [](float& a, float& b, float limit)
				{
					if (a + b > limit && a + b > 0.0f)
					{
						const float factor = limit / (a + b);
						a *= factor;
						b *= factor;
					}
				};
				fit(left, right, region.x);
				fit(top, bottom, region.y);

				float l = left * scale;
				float r = right * scale;
				float t = top * scale;
				float b = bottom * scale;
				fit(l, r, size.x);
				fit(t, b, size.y);
				const float xs[4] = { rect.Min.x, rect.Min.x + l, rect.Max.x - r, rect.Max.x };
				const float ys[4] = { rect.Min.y, rect.Min.y + t, rect.Max.y - b, rect.Max.y };
				const auto along = [](float from, float to, float t) { return from + (to - from) * t; };
				const float us[4] = { uv.x, along(uv.x, uv.z, left / region.x), along(uv.x, uv.z, 1.0f - right / region.x), uv.z };
				const float vs[4] = { uv.y, along(uv.y, uv.w, top / region.y), along(uv.y, uv.w, 1.0f - bottom / region.y), uv.w };

				for (int row = 0; row < 3; row++)
				{
					for (int column = 0; column < 3; column++)
					{
						quad(UiQuadKind::Image, widget.Texture, clip, SdfParameters{}, UiRect{ { xs[column], ys[row] }, { xs[column + 1], ys[row + 1] } },
							{ us[column], vs[row] }, { us[column + 1], vs[row + 1] }, color);
					}
				}
			}

			void draw_text(size_t i, const UiRect& rect, float opacity, const glm::vec4& clip)
			{
				const UiWidget& widget = m_Panel.Widgets[i];
				if (widget.Text.empty() || !m_Input.Fonts)
				{
					return;
				}
				const FontData* font = m_Input.Fonts(widget.Font);
				if (font == nullptr || font->PixelSize <= 0.0f)
				{
					return;
				}

				const float size = widget.FontSize * m_Layout.Scale[i];
				const std::vector<TextGlyphQuad> glyphs = layout_text(*font, widget.Text, size, rect.size(), widget.Align, widget.Wrap);

				SdfParameters sdf;
				sdf.Range = font->SdfRange;
				sdf.PixelScale = size / font->PixelSize;
				sdf.Outline = unit(widget.Outline);
				sdf.OutlineColor = ui_pack_color(with_opacity(widget.OutlineColor, opacity));
				sdf.Shadow = widget.Shadow;

				const u32 color = ui_pack_color(with_opacity(widget.Color, opacity));
				for (const TextGlyphQuad& glyph : glyphs)
				{
					quad(UiQuadKind::Sdf, widget.Font, clip, sdf, UiRect{ rect.Min + glyph.Min, rect.Min + glyph.Max }, glyph.UvMin, glyph.UvMax, color);
				}
			}

			// A colour, or a texture times a colour, over a rect.
			void fill(const UiRect& rect, const glm::vec4& color, const AssetRef& texture, const glm::vec4& clip, const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f))
			{
				if (!(color.a > 0.0f))
				{
					return;
				}
				quad(texture.valid() ? UiQuadKind::Image : UiQuadKind::Solid, texture, clip, SdfParameters{}, rect, glm::vec2(uvRect.x, uvRect.y), glm::vec2(uvRect.z, uvRect.w), color);
			}

			void quad(UiQuadKind kind, const AssetRef& texture, const glm::vec4& clip, const SdfParameters& sdf, const UiRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax, const glm::vec4& color)
			{
				quad(kind, texture, clip, sdf, rect, uvMin, uvMax, ui_pack_color(color));
			}

			void quad(UiQuadKind kind, const AssetRef& texture, const glm::vec4& clip, const SdfParameters& sdf, const UiRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax, u32 color)
			{
				const glm::vec2 size = rect.size();
				if (!(size.x > 0.0f) || !(size.y > 0.0f) || !std::isfinite(size.x) || !std::isfinite(size.y) || (color >> 24) == 0)
				{
					return;
				}

				UiDrawList& list = m_List;
				UiBatch* batch = list.Batches.empty() ? nullptr : &list.Batches.back();
				if (batch == nullptr || batch->Kind != kind || !(batch->Texture == texture) || batch->ViewTexture != m_ViewTexture || batch->ClipRect != clip
					|| batch->SdfRange != sdf.Range || batch->PixelScale != sdf.PixelScale || batch->Outline != sdf.Outline || batch->OutlineColor != sdf.OutlineColor || batch->Shadow != sdf.Shadow)
				{
					UiBatch fresh;
					fresh.Kind = kind;
					fresh.Texture = texture;
					fresh.ViewTexture = m_ViewTexture;
					fresh.ClipRect = clip;
					fresh.FirstIndex = static_cast<u32>(list.Indices.size());
					fresh.SdfRange = sdf.Range;
					fresh.PixelScale = sdf.PixelScale;
					fresh.Outline = sdf.Outline;
					fresh.OutlineColor = sdf.OutlineColor;
					fresh.Shadow = sdf.Shadow;
					list.Batches.push_back(fresh);
					batch = &list.Batches.back();
				}

				const u32 base = static_cast<u32>(list.Vertices.size());
				list.Vertices.push_back(UiVertex{ { rect.Min.x, rect.Min.y }, { uvMin.x, uvMin.y }, color });
				list.Vertices.push_back(UiVertex{ { rect.Max.x, rect.Min.y }, { uvMax.x, uvMin.y }, color });
				list.Vertices.push_back(UiVertex{ { rect.Max.x, rect.Max.y }, { uvMax.x, uvMax.y }, color });
				list.Vertices.push_back(UiVertex{ { rect.Min.x, rect.Max.y }, { uvMin.x, uvMax.y }, color });
				for (const u32 offset : { 0u, 1u, 2u, 0u, 2u, 3u })
				{
					list.Indices.push_back(base + offset);
				}
				batch->IndexCount += 6;
			}

			u32 m_ViewTexture = 0; // set while a ViewImage adds its quad
			UiDrawList& m_List;
			const UiPanelData& m_Panel;
			const std::vector<UiInteractionState>& m_Interaction;
			const UiLayoutResult& m_Layout;
			const UiLayoutInput& m_Input;
		};
	}

	std::vector<UiDrawItem> ui_draw_order(const UiPanelData& panel, const UiLayoutResult& layout, const glm::vec2& canvasSize)
	{
		std::vector<UiDrawItem> order;
		const size_t count = panel.Widgets.size();
		if (count == 0 || layout.Rects.size() != count || layout.Distance.size() != count)
		{
			return order;
		}

		std::unordered_map<UiId, size_t> indexOf;
		for (size_t i = 0; i < count; i++)
		{
			indexOf[panel.Widgets[i].Id] = i;
		}
		std::vector<std::vector<size_t>> children(count);
		std::vector<size_t> roots;
		for (size_t i = 0; i < count; i++)
		{
			const auto parent = panel.Widgets[i].Parent != 0 ? indexOf.find(panel.Widgets[i].Parent) : indexOf.end();
			(parent == indexOf.end() ? roots : children[parent->second]).push_back(i);
		}

		const glm::vec4 screen(0.0f, 0.0f, canvasSize.x, canvasSize.y);
		std::vector<size_t> worldRoots;

		// A subtree in tree order. With collect set, a world anchored widget is not visited but collected, and without it everything is.
		const auto walk = [&](size_t root, bool collect)
		{
			std::vector<UiDrawItem> stack{ UiDrawItem{ root, screen } };
			while (!stack.empty())
			{
				const UiDrawItem item = stack.back();
				stack.pop_back();
				const size_t i = item.Index;

				if (collect && panel.Widgets[i].world_anchored())
				{
					worldRoots.push_back(i);
					continue;
				}
				if (!layout.Visible[i] || !(layout.Opacity[i] > 0.0f))
				{
					continue; // its children are hidden with it
				}

				order.push_back(item);

				glm::vec4 childClip = item.Clip;
				if (clips_children(panel.Widgets[i]))
				{
					const UiRect& rect = layout.Rects[i];
					childClip = glm::vec4(std::max(childClip.x, rect.Min.x), std::max(childClip.y, rect.Min.y), std::min(childClip.z, rect.Max.x), std::min(childClip.w, rect.Max.y));
				}
				for (auto it = children[i].rbegin(); it != children[i].rend(); ++it)
				{
					stack.push_back(UiDrawItem{ *it, childClip });
				}
			}
		};

		for (const size_t root : roots)
		{
			walk(root, true);
		}

		// far to near; widgets at the same distance keep the order of the panel
		std::stable_sort(worldRoots.begin(), worldRoots.end(), [&layout](size_t a, size_t b) { return layout.Distance[a] > layout.Distance[b]; });
		for (const size_t root : worldRoots)
		{
			walk(root, false);
		}
		return order;
	}

	u32 ui_pack_color(const glm::vec4& color)
	{
		const auto channel = [](float value) { return static_cast<u32>(std::lround(finite_or(std::clamp(value, 0.0f, 1.0f), 0.0f) * 255.0f)); };
		return channel(color.r) | (channel(color.g) << 8) | (channel(color.b) << 16) | (channel(color.a) << 24);
	}

	void append_panel(UiDrawList& list, const UiPanelData& panel, const std::vector<UiInteractionState>& interaction, const UiLayoutResult& layout, const UiLayoutInput& input)
	{
		if (layout.Rects.size() != panel.Widgets.size() || layout.Distance.size() != panel.Widgets.size())
		{
			return; // not the layout of this panel
		}

		list.CanvasSize = input.CanvasSize;
		Builder(list, panel, interaction, layout, input).build();
	}
}
