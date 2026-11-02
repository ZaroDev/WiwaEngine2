#pragma once

#include <algorithm>
#include <cmath>

#include <math/math.h>

// The arithmetic of temporal anti-aliasing that the tests check on the CPU. taa.hlsl does the same on the GPU: keep them in step.
//
// Every frame the projection is shifted by a sub-pixel offset, so successive frames sample different points inside each pixel. The
// previous result (the history) is found again through the motion of the pixel, pulled into the colors the neighbourhood has now (so
// it cannot show something that is no longer there) and blended with the new frame.
namespace wiwa
{
	// Radical inverse of index in base. index >= 1.
	inline float halton(u32 index, u32 base)
	{
		float result = 0.0f;
		float fraction = 1.0f;
		while (index > 0)
		{
			fraction /= static_cast<float>(base);
			result += fraction * static_cast<float>(index % base);
			index /= base;
		}
		return result;
	}

	// The sub-pixel offset of a frame in pixels, each component in [-0.5, 0.5]: eight samples, then it repeats.
	inline glm::vec2 taa_jitter(u64 frameIndex)
	{
		const u32 index = static_cast<u32>(frameIndex % 8) + 1;
		return glm::vec2(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);
	}

	// Shifts the image by jitterPixels: positive x to the right, positive y down the screen. A perspective projection takes the shift in
	// its z column (clip.w is -z, so the offset shows up negated in NDC); an orthographic one in its translation column.
	inline glm::mat4 apply_jitter(const glm::mat4& projection, glm::vec2 jitterPixels, u32 width, u32 height)
	{
		glm::mat4 result = projection;
		const float x = 2.0f * jitterPixels.x / static_cast<float>(width);
		const float y = 2.0f * jitterPixels.y / static_cast<float>(height);
		if (projection[3][3] == 1.0f)
		{
			result[3][0] += x;
			result[3][1] -= y;
		}
		else
		{
			result[2][0] -= x;
			result[2][1] += y;
		}
		return result;
	}

	inline glm::vec3 rgb_to_ycocg(glm::vec3 rgb)
	{
		return glm::vec3(0.25f * rgb.r + 0.5f * rgb.g + 0.25f * rgb.b, 0.5f * rgb.r - 0.5f * rgb.b, -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b);
	}

	inline glm::vec3 ycocg_to_rgb(glm::vec3 ycocg)
	{
		return glm::vec3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
	}

	struct ColorBox
	{
		glm::vec3 Min{ 0.0f };
		glm::vec3 Max{ 0.0f };
	};

	// The colors the history may have: the mean of the neighbourhood plus or minus gamma standard deviations. The nine colors are
	// already in YCoCg.
	inline ColorBox taa_variance_box(const glm::vec3 (&neighbourhood)[9], float gamma)
	{
		glm::vec3 mean(0.0f);
		glm::vec3 squares(0.0f);
		for (const glm::vec3& color : neighbourhood)
		{
			mean += color;
			squares += color * color;
		}
		mean /= 9.0f;
		squares /= 9.0f;

		const glm::vec3 sigma = glm::sqrt(glm::max(squares - mean * mean, glm::vec3(0.0f)));
		return ColorBox{ mean - gamma * sigma, mean + gamma * sigma };
	}

	// Moves a color that is outside the box towards the centre of the box until it touches the surface.
	inline glm::vec3 taa_clip_to_box(glm::vec3 history, const ColorBox& box)
	{
		const glm::vec3 centre = (box.Min + box.Max) * 0.5f;
		const glm::vec3 halfExtent = glm::max((box.Max - box.Min) * 0.5f, glm::vec3(1e-6f));
		const glm::vec3 offset = history - centre;
		const glm::vec3 scaled = glm::abs(offset / halfExtent);
		const float t = std::max(scaled.x, std::max(scaled.y, scaled.z));
		return t > 1.0f ? centre + offset / t : history;
	}

	inline float taa_luma(glm::vec3 rgb)
	{
		return glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	// How much of the history is kept: less when it had to be clipped far (movedFraction 1 or more halves it) and less when the pixel
	// moves fast (16 pixels per frame or more halves it).
	inline float taa_feedback(float base, float movedFraction, float speedPixels)
	{
		return base * (1.0f - 0.5f * std::clamp(movedFraction, 0.0f, 1.0f)) * (1.0f - 0.5f * std::clamp(speedPixels / 16.0f, 0.0f, 1.0f));
	}

	// Weights both colors by 1 / (1 + luma): one very bright pixel cannot swamp the average of a pixel that is mostly dark.
	inline glm::vec3 taa_blend(glm::vec3 current, glm::vec3 history, float feedback)
	{
		const float currentWeight = (1.0f - feedback) / (1.0f + taa_luma(current));
		const float historyWeight = feedback / (1.0f + taa_luma(history));
		return (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
	}
}
