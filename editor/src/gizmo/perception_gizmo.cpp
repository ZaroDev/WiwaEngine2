#include <editor_common.h>
#include "perception_gizmo.h"

#include <cmath>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	}

	PerceptionGizmoShapes perceiver_gizmo_shapes(const Perceiver& perceiver, const glm::mat4& world, u32 rimSegments)
	{
		PerceptionGizmoShapes shapes;
		const glm::vec3 centre(world[3]);
		const glm::vec3 eye = glm::vec3(world * glm::vec4(perceiver.EyeOffset, 1.0f));
		glm::vec3 forward = -glm::vec3(world[2]);
		glm::vec3 right = glm::vec3(world[0]);
		glm::vec3 up = glm::vec3(world[1]);
		if (!finite(centre) || !finite(eye) || !finite(forward) || !finite(right) || !finite(up) || glm::length(forward) < 1.0e-6f || glm::length(right) < 1.0e-6f || glm::length(up) < 1.0e-6f)
		{
			return shapes;
		}
		forward = glm::normalize(forward);
		right = glm::normalize(right);
		up = glm::normalize(up);

		shapes.Eye = eye;
		if (perceiver.HearingRadius > 0.0f && std::isfinite(perceiver.HearingRadius))
		{
			shapes.HearingRadius = perceiver.HearingRadius;
			shapes.HearingCentre = centre;
		}

		const float range = perceiver.SightRange;
		if (!(range > 0.0f) || !std::isfinite(range))
		{
			return shapes;
		}
		const float half = glm::radians(glm::clamp(perceiver.SightAngle, 0.0f, 360.0f) * 0.5f);
		const u32 segments = std::max(rimSegments, 4u);
		if (half >= glm::pi<float>() - 1.0e-4f)
		{
			// a full circle: the rim is a circle round the eye
			shapes.Sight.push_back({ eye, eye + forward * range });
			shapes.Sight.push_back({ eye, eye - forward * range });
			return shapes;
		}

		// the far rim: a circle at the end of the cone (a cone of half angle `half` reaches a circle of radius range * sin(half) at depth range * cos(half))
		const glm::vec3 centreOfRim = eye + forward * (range * std::cos(half));
		const float radius = range * std::sin(half);
		glm::vec3 previous = centreOfRim + right * radius;
		for (u32 i = 1; i <= segments; i++)
		{
			const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
			const glm::vec3 point = centreOfRim + (right * std::cos(angle) + up * std::sin(angle)) * radius;
			shapes.Sight.push_back({ previous, point });
			if (i % (segments / 4) == 0)
			{
				shapes.Sight.push_back({ eye, point }); // an edge line at each quarter
			}
			previous = point;
		}
		shapes.Sight.push_back({ eye, eye + forward * range }); // the middle
		return shapes;
	}
}
