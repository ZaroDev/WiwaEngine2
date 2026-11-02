#include <pch.h>
#include "ui_layout.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ui_font.h"

namespace wiwa
{
	namespace
	{
		constexpr float MIN_W = 1e-6f;

		struct Pending
		{
			size_t Index = 0;
			std::optional<UiRect> Forced{}; // set by the stack the widget is in
		};

		float safe(float value)
		{
			return std::isfinite(value) ? value : 0.0f;
		}

		glm::vec2 safe(glm::vec2 value)
		{
			return { safe(value.x), safe(value.y) };
		}

		// What the widget asks for as a size, in pixels (offsets, or its text when it has none).
		glm::vec2 preferred_size(const UiWidget& widget, float scale, const UiLayoutInput& input, float maxWidth)
		{
			glm::vec2 size = (widget.OffsetMax - widget.OffsetMin) * scale;
			if (widget.Kind == WidgetKind::Text && (size.x <= 0.0f || size.y <= 0.0f) && input.Fonts)
			{
				if (const FontData* font = input.Fonts(widget.Font))
				{
					const glm::vec2 measured = measure_text(*font, widget.Text, widget.FontSize * scale, widget.Wrap ? maxWidth : 0.0f, widget.Wrap);
					size.x = size.x <= 0.0f ? measured.x : size.x;
					size.y = size.y <= 0.0f ? measured.y : size.y;
				}
			}
			return { std::max(size.x, 0.0f), std::max(size.y, 0.0f) };
		}

		UiRect rect_from_anchors(const UiWidget& widget, const UiRect& parent, float scale)
		{
			const glm::vec2 anchorMin = glm::min(widget.AnchorMin, widget.AnchorMax);
			const glm::vec2 anchorMax = glm::max(widget.AnchorMin, widget.AnchorMax);
			const glm::vec2 size = parent.size();

			UiRect rect;
			rect.Min = parent.Min + anchorMin * size + widget.OffsetMin * scale;
			rect.Max = parent.Min + anchorMax * size + widget.OffsetMax * scale;
			if (rect.Max.x < rect.Min.x)
			{
				rect.Max.x = rect.Min.x;
			}
			if (rect.Max.y < rect.Min.y)
			{
				rect.Max.y = rect.Min.y;
			}
			return rect;
		}

		// The pixel position of a point on the screen of a camera; nothing if it cannot be projected.
		struct Projected
		{
			glm::vec2 Screen{ 0.0f };
			bool Behind = false;
		};

		Projected project(const UiCameraView& camera, const glm::vec3& world, const glm::vec2& canvas)
		{
			const glm::vec4 clip = camera.ViewProjection * glm::vec4(world, 1.0f);
			Projected result;
			float w = clip.w;
			if (w <= MIN_W)
			{
				result.Behind = true;
				w = std::max(std::abs(w), MIN_W); // mirrored, so a point behind the camera still has a direction
				const glm::vec2 ndc = -glm::vec2(clip.x, clip.y) / w;
				result.Screen = { (ndc.x * 0.5f + 0.5f) * canvas.x, (0.5f - ndc.y * 0.5f) * canvas.y };
			}
			else
			{
				const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / w;
				result.Screen = { (ndc.x * 0.5f + 0.5f) * canvas.x, (0.5f - ndc.y * 0.5f) * canvas.y };
			}
			return result;
		}
	}

	float ui_canvas_scale(const UiPanelData& panel, const glm::vec2& canvasSize)
	{
		if (!(canvasSize.x > 0.0f) || !(canvasSize.y > 0.0f) || !(panel.ReferenceResolution.x > 0.0f) || !(panel.ReferenceResolution.y > 0.0f))
		{
			return 0.0f;
		}

		const float match = std::clamp(panel.MatchWidthOrHeight, 0.0f, 1.0f);
		const float scale = std::pow(canvasSize.x / panel.ReferenceResolution.x, 1.0f - match) * std::pow(canvasSize.y / panel.ReferenceResolution.y, match);
		return safe(scale);
	}

	UiLayoutResult compute_layout(const UiPanelData& panel, const UiLayoutInput& input)
	{
		const size_t count = panel.Widgets.size();
		UiLayoutResult result;
		result.Rects.assign(count, UiRect{});
		result.Visible.assign(count, 0);
		result.Opacity.assign(count, 0.0f);
		result.Scale.assign(count, 0.0f);
		result.CanvasScale = ui_canvas_scale(panel, input.CanvasSize);
		if (count == 0 || result.CanvasScale <= 0.0f)
		{
			return result;
		}

		// children by parent id, in array order
		std::unordered_map<UiId, size_t> indexOf;
		indexOf.reserve(count);
		for (size_t i = 0; i < count; i++)
		{
			indexOf[panel.Widgets[i].Id] = i;
		}
		std::vector<std::vector<size_t>> children(count);
		std::vector<size_t> roots;
		for (size_t i = 0; i < count; i++)
		{
			const auto parent = panel.Widgets[i].Parent != 0 ? indexOf.find(panel.Widgets[i].Parent) : indexOf.end();
			if (parent == indexOf.end())
			{
				roots.push_back(i);
			}
			else
			{
				children[parent->second].push_back(i);
			}
		}

		const UiRect canvasRect{ glm::vec2(0.0f), input.CanvasSize };
		std::vector<Pending> work;
		for (auto it = roots.rbegin(); it != roots.rend(); ++it)
		{
			work.push_back(Pending{ *it, std::nullopt });
		}

		// the parent state of each widget, filled as the parent is done
		std::vector<UiRect> parentRect(count, canvasRect);
		std::vector<float> parentScale(count, result.CanvasScale);
		std::vector<u8> parentVisible(count, 1);
		std::vector<float> parentOpacity(count, 1.0f);
		std::vector<u8> done(count, 0);

		while (!work.empty())
		{
			const Pending pending = work.back();
			work.pop_back();
			const size_t i = pending.Index;
			if (done[i])
			{
				continue; // cannot happen for a validated panel (no loops); guards a hand-built one
			}
			done[i] = 1;

			const UiWidget& widget = panel.Widgets[i];
			bool visible = parentVisible[i] != 0 && widget.Visible;
			float opacity = parentOpacity[i] * std::clamp(safe(widget.Opacity), 0.0f, 1.0f);
			float scale = parentScale[i];
			UiRect rect;

			if (widget.world_anchored())
			{
				scale = parentScale[i];
				visible = visible && input.Camera != nullptr && input.Camera->Valid && static_cast<bool>(input.WorldPosition);
				std::optional<glm::vec3> position;
				if (visible)
				{
					position = input.WorldPosition(widget.WorldEntity);
					visible = position.has_value();
				}

				if (visible)
				{
					const glm::vec3 world = *position + widget.WorldOffset;
					const Projected projected = project(*input.Camera, world, input.CanvasSize);
					const float distance = glm::length(world - input.Camera->Position);

					if (widget.WorldScaleWithDistance)
					{
						const float lo = std::min(widget.WorldMinScale, widget.WorldMaxScale);
						const float hi = std::max(widget.WorldMinScale, widget.WorldMaxScale);
						scale *= std::clamp(safe(widget.WorldRefDistance) / std::max(distance, 1e-4f), lo, hi);
					}
					if (widget.WorldFadeEnd > widget.WorldFadeStart)
					{
						opacity *= std::clamp(1.0f - (distance - widget.WorldFadeStart) / (widget.WorldFadeEnd - widget.WorldFadeStart), 0.0f, 1.0f);
					}

					glm::vec2 point = projected.Screen;
					const bool outside = point.x < 0.0f || point.y < 0.0f || point.x > input.CanvasSize.x || point.y > input.CanvasSize.y;
					if (projected.Behind && (widget.WorldHideBehind || widget.WorldOffscreen == UiOffscreen::Hide))
					{
						visible = false;
					}
					else if (outside)
					{
						if (widget.WorldOffscreen == UiOffscreen::Hide)
						{
							visible = false;
						}
						else
						{
							point = glm::clamp(point, glm::vec2(0.0f), input.CanvasSize);
						}
					}

					point = safe(point) + widget.WorldScreenOffset * result.CanvasScale;
					const glm::vec2 size = glm::max(widget.OffsetMax - widget.OffsetMin, glm::vec2(0.0f)) * scale;
					rect.Min = point - glm::clamp(widget.Pivot, glm::vec2(0.0f), glm::vec2(1.0f)) * size;
					rect.Max = rect.Min + size;
				}
			}
			else if (pending.Forced)
			{
				rect = *pending.Forced;
			}
			else
			{
				rect = rect_from_anchors(widget, parentRect[i], scale);
			}

			rect.Min = safe(rect.Min);
			rect.Max = safe(rect.Max);

			result.Rects[i] = rect;
			result.Visible[i] = visible ? 1 : 0;
			result.Opacity[i] = visible ? opacity : 0.0f;
			result.Scale[i] = scale;

			const std::vector<size_t>& kids = children[i];
			for (const size_t child : kids)
			{
				parentRect[child] = rect;
				parentScale[child] = scale;
				parentVisible[child] = visible ? 1 : 0;
				parentOpacity[child] = visible ? opacity : 0.0f;
			}

			// stacks place the children that are in the flow; the rest go by their own anchors
			std::vector<Pending> next;
			if (widget.Kind == WidgetKind::HStack || widget.Kind == WidgetKind::VStack)
			{
				const bool horizontal = widget.Kind == WidgetKind::HStack;
				const glm::vec4 padding = widget.Padding * scale;
				UiRect inner;
				inner.Min = rect.Min + glm::vec2(padding.x, padding.y);
				inner.Max = glm::max(rect.Max - glm::vec2(padding.z, padding.w), inner.Min);
				const float spacing = widget.Spacing * scale;

				std::vector<size_t> flow;
				for (const size_t child : kids)
				{
					if (!panel.Widgets[child].world_anchored() && panel.Widgets[child].Visible)
					{
						flow.push_back(child);
					}
				}

				const int main = horizontal ? 0 : 1;
				const int cross = 1 - main;
				const float innerMain = inner.size()[main];
				const float innerCross = inner.size()[cross];

				std::vector<glm::vec2> sizes;
				float total = flow.empty() ? 0.0f : spacing * static_cast<float>(flow.size() - 1);
				for (const size_t child : flow)
				{
					const glm::vec2 asked = preferred_size(panel.Widgets[child], scale, input, horizontal ? 0.0f : innerCross);
					sizes.push_back(asked);
					total += asked[main];
				}

				const float extra = widget.ForceExpand && !flow.empty() ? std::max(innerMain - total, 0.0f) / static_cast<float>(flow.size()) : 0.0f;
				float cursor = inner.Min[main];
				for (size_t k = 0; k < flow.size(); k++)
				{
					glm::vec2 size = sizes[k];
					size[main] += extra;
					if (widget.ForceExpand)
					{
						size[cross] = innerCross;
					}

					float crossStart = inner.Min[cross];
					if (widget.ChildAlign == UiChildAlign::Center)
					{
						crossStart += (innerCross - size[cross]) * 0.5f;
					}
					else if (widget.ChildAlign == UiChildAlign::End)
					{
						crossStart += innerCross - size[cross];
					}

					UiRect forced;
					forced.Min[main] = cursor;
					forced.Min[cross] = crossStart;
					forced.Max = forced.Min + size;
					cursor += size[main] + spacing;
					next.push_back(Pending{ flow[k], forced });
				}

				for (const size_t child : kids)
				{
					if (std::find(flow.begin(), flow.end(), child) == flow.end())
					{
						next.push_back(Pending{ child, std::nullopt });
					}
				}
			}
			else
			{
				for (const size_t child : kids)
				{
					next.push_back(Pending{ child, std::nullopt });
				}
			}

			for (auto it = next.rbegin(); it != next.rend(); ++it)
			{
				work.push_back(*it);
			}
		}

		return result;
	}
}
