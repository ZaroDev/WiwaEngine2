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

		bool usable(const Ray& ray)
		{
			return finite(ray.Origin) && finite(ray.Direction) && glm::dot(ray.Direction, ray.Direction) > EPSILON;
		}

		Ray default_ray() { return Ray{}; }

		// A hit on a handle: how far along the ray it is.
		struct Hit
		{
			Handle Which = Handle::None;
			float Depth = 0.0f;

			void offer(Handle handle, float depth)
			{
				if (Which == Handle::None || depth < Depth)
				{
					Which = handle;
					Depth = depth;
				}
			}
		};

		// The axis' handles are segments along unit axis `u` from start to end (world units); a segment is hit when the ray passes within radius of it.
		bool hits_segment(const Ray& ray, glm::vec3 origin, glm::vec3 u, float start, float end, float radius, float& depth)
		{
			const float t = std::clamp(axis_parameter(ray, origin, u), start, end);
			const glm::vec3 point = origin + u * t;
			const glm::vec3 toPoint = point - ray.Origin;
			depth = glm::dot(toPoint, ray.Direction);
			if (depth < 0.0f)
			{
				return false;
			}
			return glm::length(toPoint - ray.Direction * depth) <= radius;
		}
	}

	Ray screen_ray(glm::vec2 pixel, glm::vec2 viewportSize, const glm::mat4& view, const glm::mat4& projection)
	{
		if (!finite(pixel.x) || !finite(pixel.y) || !finite(viewportSize.x) || !finite(viewportSize.y) || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f
			|| !finite(view) || !finite(projection))
		{
			return default_ray();
		}

		const glm::mat4 viewProjection = projection * view;
		const float determinant = glm::determinant(viewProjection);
		if (!finite(determinant) || std::abs(determinant) < 1e-30f)
		{
			return default_ray();
		}

		const glm::mat4 inverse = glm::inverse(viewProjection);
		const float ndcX = pixel.x / viewportSize.x * 2.0f - 1.0f;
		const float ndcY = 1.0f - pixel.y / viewportSize.y * 2.0f;
		const glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		const glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) < EPSILON || std::abs(farPoint.w) < EPSILON)
		{
			return default_ray();
		}

		const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
		const glm::vec3 direction = glm::vec3(farPoint) / farPoint.w - origin;
		if (!finite(origin) || !finite(direction) || glm::dot(direction, direction) < EPSILON * EPSILON)
		{
			return default_ray();
		}
		return Ray{ origin, glm::normalize(direction) };
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

	std::optional<float> ray_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal)
	{
		if (!usable(ray) || !finite(point) || !finite(normal))
		{
			return std::nullopt;
		}

		const float denominator = glm::dot(ray.Direction, normal);
		if (std::abs(denominator) < EPSILON)
		{
			return std::nullopt;
		}
		const float t = glm::dot(point - ray.Origin, normal) / denominator;
		if (!finite(t) || t < 0.0f)
		{
			return std::nullopt;
		}
		return t;
	}

	float axis_parameter(const Ray& ray, glm::vec3 axisOrigin, glm::vec3 axisDirection)
	{
		if (!usable(ray) || !finite(axisOrigin) || !finite(axisDirection) || glm::dot(axisDirection, axisDirection) < EPSILON)
		{
			return 0.0f;
		}

		// Closest points of the lines P(s) = ray.Origin + s * d and Q(t) = axisOrigin + t * u.
		const glm::vec3 d = ray.Direction;
		const glm::vec3 u = glm::normalize(axisDirection);
		const glm::vec3 w = ray.Origin - axisOrigin;
		const float b = glm::dot(d, u);
		const float denominator = 1.0f - b * b; // |d| = |u| = 1
		if (denominator < 1e-8f)
		{
			return 0.0f; // parallel
		}
		const float t = (glm::dot(d, d) * glm::dot(u, w) - b * glm::dot(d, w)) / denominator;
		return finite(t) ? t : 0.0f;
	}

	float signed_angle(glm::vec3 from, glm::vec3 to, glm::vec3 axis)
	{
		if (!finite(from) || !finite(to) || !finite(axis) || glm::dot(axis, axis) < EPSILON)
		{
			return 0.0f;
		}

		const glm::vec3 n = glm::normalize(axis);
		const glm::vec3 a = from - n * glm::dot(from, n);
		const glm::vec3 b = to - n * glm::dot(to, n);
		if (glm::dot(a, a) < EPSILON * EPSILON || glm::dot(b, b) < EPSILON * EPSILON)
		{
			return 0.0f;
		}

		const float angle = std::atan2(glm::dot(glm::cross(a, b), n), glm::dot(a, b));
		return finite(angle) ? angle : 0.0f;
	}

	float snap(float value, float step)
	{
		if (!finite(value) || !finite(step) || step <= 0.0f)
		{
			return value;
		}
		return std::round(value / step) * step;
	}

	Handle pick_handle(Mode mode, const Ray& ray, const Frame& frame, float hitRadiusWorld)
	{
		if (!usable(ray) || !finite(frame.Origin) || !finite(frame.Scale) || frame.Scale <= 0.0f || !finite(hitRadiusWorld) || hitRadiusWorld < 0.0f)
		{
			return Handle::None;
		}

		glm::vec3 axes[3];
		for (int i = 0; i < 3; i++)
		{
			axes[i] = frame.Axes[i];
			if (!finite(axes[i]) || glm::dot(axes[i], axes[i]) < EPSILON)
			{
				return Handle::None;
			}
			axes[i] = glm::normalize(axes[i]);
		}

		const glm::vec3 origin = frame.Origin;
		const float scale = frame.Scale;
		constexpr Handle AXIS_HANDLES[3] = { Handle::AxisX, Handle::AxisY, Handle::AxisZ };
		Hit hit;

		switch (mode)
		{
		case Mode::Translate:
		{
			for (int i = 0; i < 3; i++)
			{
				float depth = 0.0f;
				if (hits_segment(ray, origin, axes[i], AXIS_START * scale, ARROW_LENGTH * scale, hitRadiusWorld, depth))
				{
					hit.offer(AXIS_HANDLES[i], depth);
				}
			}
			if (hit.Which != Handle::None)
			{
				return hit.Which;
			}

			// The square on the plane with normal axes[n] spans PLANE_MIN..PLANE_MAX along the two other axes.
			constexpr Handle PLANE_HANDLES[3] = { Handle::PlaneYZ, Handle::PlaneXZ, Handle::PlaneXY };
			const float slack = hitRadiusWorld / scale;
			for (int n = 0; n < 3; n++)
			{
				const std::optional<float> t = ray_plane(ray, origin, axes[n]);
				if (!t)
				{
					continue;
				}
				const glm::vec3 offset = ray.Origin + ray.Direction * *t - origin;
				const float u = glm::dot(offset, axes[(n + 1) % 3]) / scale;
				const float v = glm::dot(offset, axes[(n + 2) % 3]) / scale;
				if (u >= PLANE_MIN - slack && u <= PLANE_MAX + slack && v >= PLANE_MIN - slack && v <= PLANE_MAX + slack)
				{
					hit.offer(PLANE_HANDLES[n], *t);
				}
			}
			return hit.Which;
		}

		case Mode::Scale:
		{
			// The uniform handle is a sphere around the origin.
			const glm::vec3 toOrigin = origin - ray.Origin;
			const float along = glm::dot(toOrigin, ray.Direction);
			if (along >= 0.0f && glm::length(toOrigin - ray.Direction * along) <= UNIFORM_RADIUS * scale + hitRadiusWorld)
			{
				return Handle::Uniform;
			}

			for (int i = 0; i < 3; i++)
			{
				float depth = 0.0f;
				if (hits_segment(ray, origin, axes[i], UNIFORM_RADIUS * scale, ARROW_LENGTH * scale, hitRadiusWorld, depth))
				{
					hit.offer(AXIS_HANDLES[i], depth);
				}
			}
			return hit.Which;
		}

		case Mode::Rotate:
		{
			for (int i = 0; i < 3; i++)
			{
				const std::optional<float> t = ray_plane(ray, origin, axes[i]);
				if (!t)
				{
					continue;
				}
				const glm::vec3 offset = ray.Origin + ray.Direction * *t - origin;
				if (std::abs(glm::length(offset) - RING_RADIUS * scale) <= hitRadiusWorld)
				{
					hit.offer(AXIS_HANDLES[i], *t);
				}
			}
			return hit.Which;
		}
		}
		return Handle::None;
	}
}
