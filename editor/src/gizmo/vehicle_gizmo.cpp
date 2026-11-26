#include <editor_common.h>
#include "vehicle_gizmo.h"

#include <cmath>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	}

	VehicleWheelGizmoShapes vehicle_wheel_gizmo_shapes(const VehicleWheel& wheel, const glm::mat4& world, u32 segments)
	{
		VehicleWheelGizmoShapes shapes;
		const glm::vec3 centre(world[3]);
		glm::vec3 up(world[1]);
		glm::vec3 forward(-world[2]);
		if (!finite(centre) || !finite(up) || !finite(forward) || glm::length(up) < 1e-6f || glm::length(forward) < 1e-6f || segments < 3)
		{
			return shapes;
		}
		up = glm::normalize(up);
		forward = glm::normalize(forward);
		shapes.Circle.reserve(segments);
		for (u32 i = 0; i < segments; i++)
		{
			const float angle = 6.28318531f * static_cast<float>(i) / static_cast<float>(segments);
			shapes.Circle.push_back(centre + wheel.Radius * (std::cos(angle) * up + std::sin(angle) * forward));
		}
		const float half = 0.5f * (wheel.SuspensionMaxLength - wheel.SuspensionMinLength);
		shapes.TravelTop = centre + up * half;
		shapes.TravelBottom = centre - up * half;
		return shapes;
	}
}
