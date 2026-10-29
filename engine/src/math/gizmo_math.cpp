#include <pch.h>
#include "gizmo_math.h"

#include <algorithm>
#include <cmath>

namespace wiwa::gizmo
{
	namespace
	{
		constexpr float EPSILON = 1e-6f;

		bool finite(float value) { return std::isfinite(value); }
		bool finite(const glm::vec3& v) { return finite(v.x) && finite(v.y) && finite(v.z); }
		bool finite(const glm::mat4& m)
		{
			for (int c = 0; c < 4; c++)
			{
				for (int r = 0; r < 4; r++)
				{
					if (!finite(m[c][r]))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	float screen_constant_scale(glm::vec3 worldPosition, const glm::mat4& view, const glm::mat4& projection, float pixels, float viewportHeight)
	{
		if (!finite(worldPosition) || !finite(view) || !finite(projection) || !finite(pixels) || !finite(viewportHeight) || pixels <= 0.0f || viewportHeight <= 0.0f)
		{
			return 1.0f;
		}

		const float focal = projection[1][1]; // cot(half the vertical field of view), or 2 / height for orthographic
		if (!finite(focal) || std::abs(focal) < EPSILON)
		{
			return 1.0f;
		}

		const bool perspective = projection[2][3] != 0.0f;
		float distance = 1.0f; // orthographic: the same size at every depth
		if (perspective)
		{
			distance = std::max(std::abs((view * glm::vec4(worldPosition, 1.0f)).z), 1e-3f);
		}
		const float scale = pixels * 2.0f * distance / (std::abs(focal) * viewportHeight);
		return finite(scale) && scale > 0.0f ? scale : 1.0f;
	}
}
