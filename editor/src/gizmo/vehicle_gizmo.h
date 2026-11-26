#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <scene/components.h>

namespace wiwa
{
	struct VehicleWheelGizmoShapes
	{
		std::vector<glm::vec3> Circle{}; // the tyre's outline, closed (the last point joins the first), in the plane across the axle
		glm::vec3 TravelTop{ 0.0f };     // the suspension travel through the centre, along the entity's up
		glm::vec3 TravelBottom{ 0.0f };
	};

	// What the viewport draws for a selected VehicleWheel: its radius round the entity's position (the axle is the entity's x) and its suspension
	// travel (MaxLength - MinLength, centred on the wheel: the authored position is the ride height). Empty for a transform that is not finite.
	VehicleWheelGizmoShapes vehicle_wheel_gizmo_shapes(const VehicleWheel& wheel, const glm::mat4& world, u32 segments = 24);
}
