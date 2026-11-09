#pragma once

#include <algorithm>
#include <cmath>

#include "../scene/render_scene.h"

namespace wiwa
{
	// A camera orbiting a point, for views of a single object (previews, thumbnails, UI widgets). yaw 0 looks from +Z towards the target and
	// turns the camera about the vertical axis; pitch (degrees) lifts it above the target (negative looks up from below), clamped short of the poles.
	inline RenderCamera make_orbit_camera(const glm::vec3& target, float distance, float yawDegrees, float pitchDegrees, float fovDegrees, float aspect,
		float nearPlane = 0.05f, float farPlane = 500.0f)
	{
		distance = std::isfinite(distance) ? std::max(distance, 0.01f) : 3.0f;
		const float yaw = glm::radians(std::isfinite(yawDegrees) ? yawDegrees : 0.0f);
		const float pitch = glm::radians(std::clamp(std::isfinite(pitchDegrees) ? pitchDegrees : 0.0f, -89.0f, 89.0f));
		const float fov = std::isfinite(fovDegrees) ? std::clamp(fovDegrees, 1.0f, 179.0f) : 35.0f;
		if (!std::isfinite(aspect) || aspect <= 0.0f)
		{
			aspect = 1.0f;
		}

		RenderCamera camera;
		camera.Position = target + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
		camera.View = glm::lookAtRH(camera.Position, target, glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Near = nearPlane;
		camera.Far = farPlane;
		camera.Projection = glm::perspectiveRH_ZO(glm::radians(fov), aspect, nearPlane, farPlane);
		return camera;
	}
}
