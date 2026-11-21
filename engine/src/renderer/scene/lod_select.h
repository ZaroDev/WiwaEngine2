#pragma once

#include <algorithm>
#include <cmath>

#include "../resources/gpu_mesh.h"

namespace wiwa
{
	// What picking a level of detail needs of the view: how many pixels one world unit covers one unit away, and where the eye is.
	struct LodView
	{
		glm::vec3 CameraPosition{ 0.0f };
		float PixelsPerUnit = 0.0f;  // at distance 1: half the viewport height times the projection's y scale (perspective); at any distance (orthographic)
		bool Orthographic = false;
		float PixelError = 0.0f;     // the screen space error a level may have; 0 turns levels off (every primitive draws its full detail)
	};

	// The coarsest level of the primitive whose error, seen from the camera, stays under the view's pixel error. Level 0 is the mesh as made. A level's Error
	// is the distance (in mesh space) its surface may stray from the full one, so the error on screen is that times the instance's largest scale, times
	// the pixels one unit covers at the distance of the nearest point of the primitive's bounding sphere.
	inline u32 select_mesh_lod(const GpuPrimitive& primitive, const glm::mat4& world, const LodView& view)
	{
		if (primitive.LodCount <= 1 || view.PixelError <= 0.0f || view.PixelsPerUnit <= 0.0f)
		{
			return 0;
		}

		const float scaleX = glm::length(glm::vec3(world[0]));
		const float scaleY = glm::length(glm::vec3(world[1]));
		const float scaleZ = glm::length(glm::vec3(world[2]));
		const float scale = std::max(scaleX, std::max(scaleY, scaleZ));

		float pixelsPerUnit = view.PixelsPerUnit;
		if (!view.Orthographic)
		{
			const glm::vec3 centre = (primitive.BoundsMin + primitive.BoundsMax) * 0.5f;
			const float radius = glm::length(primitive.BoundsMax - primitive.BoundsMin) * 0.5f * scale;
			const float distance = glm::length(glm::vec3(world * glm::vec4(centre, 1.0f)) - view.CameraPosition) - radius;
			pixelsPerUnit /= std::max(distance, 0.01f);
		}

		for (u32 level = primitive.LodCount - 1; level > 0; level--)
		{
			if (primitive.Lods[level].Error * scale * pixelsPerUnit <= view.PixelError)
			{
				return level;
			}
		}
		return 0;
	}
}
