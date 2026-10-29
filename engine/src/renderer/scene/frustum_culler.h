#pragma once

#include <array>
#include <cmath>

#include <math/math.h>

namespace wiwa
{
	// The six planes of a view frustum (depth 0..1, as the projections here make it), for testing boxes against. Plane normals point
	// inside; a point is inside when dot(plane.xyz, p) + plane.w >= 0 for all six.
	class Frustum
	{
	public:
		explicit Frustum(const glm::mat4& viewProjection)
		{
			const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
			const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
			const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]);
			const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);
			m_Planes[0] = row3 + row0; // left
			m_Planes[1] = row3 - row0; // right
			m_Planes[2] = row3 + row1; // bottom
			m_Planes[3] = row3 - row1; // top
			m_Planes[4] = row2;        // near (depth >= 0)
			m_Planes[5] = row3 - row2; // far
		}

		// Whether the box (given in the local space of `world`) may be seen. A box that touches the frustum, or whose test is not
		// certain, counts as visible; a matrix or box with a NaN or infinity is never visible.
		bool intersects(const glm::mat4& world, const glm::vec3& boundsMin, const glm::vec3& boundsMax) const
		{
			if (!finite(world) || !finite(boundsMin) || !finite(boundsMax))
			{
				return false;
			}

			glm::vec3 corners[8];
			for (int i = 0; i < 8; i++)
			{
				const glm::vec3 local((i & 1) ? boundsMax.x : boundsMin.x, (i & 2) ? boundsMax.y : boundsMin.y, (i & 4) ? boundsMax.z : boundsMin.z);
				corners[i] = glm::vec3(world * glm::vec4(local, 1.0f));
			}

			for (const glm::vec4& plane : m_Planes)
			{
				bool allOutside = true;
				for (const glm::vec3& corner : corners)
				{
					if (glm::dot(glm::vec3(plane), corner) + plane.w >= 0.0f)
					{
						allOutside = false;
						break;
					}
				}
				if (allOutside)
				{
					return false;
				}
			}
			return true;
		}

	private:
		static bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		static bool finite(const glm::mat4& m)
		{
			for (int c = 0; c < 4; c++)
			{
				for (int r = 0; r < 4; r++)
				{
					if (!std::isfinite(m[c][r]))
					{
						return false;
					}
				}
			}
			return true;
		}

		std::array<glm::vec4, 6> m_Planes{};
	};

	class FrustumCuller
	{
	public:
		// One-off test; use a Frustum when testing many boxes against the same camera.
		static bool is_visible(const glm::mat4& viewProjection, const glm::mat4& world, glm::vec3 boundsMin, glm::vec3 boundsMax)
		{
			return Frustum(viewProjection).intersects(world, boundsMin, boundsMax);
		}
	};
}
