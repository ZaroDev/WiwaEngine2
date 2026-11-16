#include <pch.h>
#include "ui_video_fit.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		bool finite4(const glm::vec4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }

		float finite_or_zero(float v) { return std::isfinite(v) ? v : 0.0f; }
	}

	UiVideoRects ui_video_fit(const glm::vec4& rawRect, glm::vec2 videoSize, UiVideoFit fit)
	{
		// a rect is its corner and a size that is not negative
		glm::vec4 rect{ finite_or_zero(rawRect.x), finite_or_zero(rawRect.y), finite_or_zero(rawRect.z), finite_or_zero(rawRect.w) };
		rect.z = std::max(rect.z, rect.x);
		rect.w = std::max(rect.w, rect.y);

		UiVideoRects out;
		out.Picture = rect;

		const float width = rect.z - rect.x;
		const float height = rect.w - rect.y;
		const bool usable = std::isfinite(videoSize.x) && std::isfinite(videoSize.y) && videoSize.x > 0.0f && videoSize.y > 0.0f
			&& std::isfinite(width) && std::isfinite(height) && width > 0.0f && height > 0.0f;
		if (!usable || fit == UiVideoFit::Stretch)
		{
			return out;
		}

		const float scaleX = width / videoSize.x;
		const float scaleY = height / videoSize.y;
		if (fit == UiVideoFit::Letterbox)
		{
			const float scale = std::min(scaleX, scaleY);
			const float pictureW = videoSize.x * scale;
			const float pictureH = videoSize.y * scale;
			const float x0 = rect.x + (width - pictureW) * 0.5f;
			const float y0 = rect.y + (height - pictureH) * 0.5f;
			out.Picture = { x0, y0, x0 + pictureW, y0 + pictureH };
			if (scaleX > scaleY)
			{
				out.BarA = { rect.x, rect.y, x0, rect.w };            // left
				out.BarB = { x0 + pictureW, rect.y, rect.z, rect.w }; // right
			}
			else
			{
				out.BarA = { rect.x, rect.y, rect.z, y0 };            // top
				out.BarB = { rect.x, y0 + pictureH, rect.z, rect.w }; // bottom
			}
		}
		else
		{
			const float scale = std::max(scaleX, scaleY);
			const float shownU = std::clamp(width / (videoSize.x * scale), 0.0f, 1.0f);
			const float shownV = std::clamp(height / (videoSize.y * scale), 0.0f, 1.0f);
			out.Uv = { (1.0f - shownU) * 0.5f, (1.0f - shownV) * 0.5f, (1.0f + shownU) * 0.5f, (1.0f + shownV) * 0.5f };
		}

		if (!finite4(out.Picture) || !finite4(out.Uv) || !finite4(out.BarA) || !finite4(out.BarB))
		{
			return UiVideoRects{ rect, { 0.0f, 0.0f, 1.0f, 1.0f }, glm::vec4(0.0f), glm::vec4(0.0f) };
		}
		return out;
	}
}
