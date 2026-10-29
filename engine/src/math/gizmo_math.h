#pragma once

#include <optional>

#include "math.h"

// The math of the transform gizmo: turning a pixel into a ray, hit testing the handles, and the helpers a drag uses. Pure functions: no
// ImGui, no GPU, no scene. Every one of them returns a finite value (or nullopt / Handle::None) for input that cannot be used: NaN, a zero
// size, a ray parallel to what it should hit.
namespace wiwa::gizmo
{
	// The shape of the handles, in units of Frame::Scale. The editor draws with the same numbers.
	constexpr float ARROW_LENGTH = 1.0f;   // translate arrows and scale handles reach this far along their axis
	constexpr float AXIS_START = 0.15f;    // translate arrows start here
	constexpr float PLANE_MIN = 0.3f;      // the plane squares span this ...
	constexpr float PLANE_MAX = 0.6f;      // ... to this along both of their axes
	constexpr float UNIFORM_RADIUS = 0.2f; // the uniform scale handle: a sphere around the origin
	constexpr float RING_RADIUS = 1.0f;    // the rotate rings

	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f }; // unit length
	};

	enum class Mode : u8 { Translate, Rotate, Scale };
	enum class Space : u8 { World, Local };

	// PlaneYZ is the square on the plane whose normal is X, and so on.
	enum class Handle : u8 { None, AxisX, AxisY, AxisZ, PlaneYZ, PlaneXZ, PlaneXY, Uniform };

	// Where the gizmo is: its origin, its three unit axes (columns X, Y, Z: the world axes or the entity's) and the world length of one handle unit.
	struct Frame
	{
		glm::vec3 Origin{ 0.0f };
		glm::mat3 Axes{ 1.0f };
		float Scale = 1.0f;
	};

	// The ray through a pixel of the viewport ((0, 0) is the top left): it starts on the near plane. Depth range 0..1.
	Ray screen_ray(glm::vec2 pixel, glm::vec2 viewportSize, const glm::mat4& view, const glm::mat4& projection);

	// The world size that covers `pixels` pixels of the viewport's height at the position, so a gizmo keeps its size on screen. Works for
	// perspective and orthographic projections; a position behind or at the camera uses its distance, never zero.
	float screen_constant_scale(glm::vec3 worldPosition, const glm::mat4& view, const glm::mat4& projection, float pixels, float viewportHeight);

	// The distance along the ray to the plane through `point` with `normal`; nullopt when the ray is parallel to the plane or the plane is behind it.
	std::optional<float> ray_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal);

	// Where on an axis line (origin + t * direction, direction a unit vector) the ray comes closest: t. 0 when the ray is parallel to it.
	float axis_parameter(const Ray& ray, glm::vec3 axisOrigin, glm::vec3 axisDirection);

	// The angle in radians (-pi..pi) that turns `from` into `to` about `axis`, looking at the axis from its tip counterclockwise being positive.
	// Components along the axis are ignored.
	float signed_angle(glm::vec3 from, glm::vec3 to, glm::vec3 axis);

	// value rounded to the nearest multiple of step; a step that is not positive leaves it alone.
	float snap(float value, float step);

	// Which handle of the mode's gizmo the ray goes through (the nearest to the ray's origin); hitRadiusWorld is how close counts as through.
	//  Translate: the three arrows (AXIS_START..ARROW_LENGTH), then the three plane squares (PLANE_MIN..PLANE_MAX).
	//  Scale: the uniform sphere (UNIFORM_RADIUS), then the three axes (UNIFORM_RADIUS..ARROW_LENGTH).
	//  Rotate: the three rings (RING_RADIUS) around the axes.
	Handle pick_handle(Mode mode, const Ray& ray, const Frame& frame, float hitRadiusWorld);
}
