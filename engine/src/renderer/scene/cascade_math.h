#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <math/math.h>

#include "render_scene.h"

// The matrices of the sun's cascaded shadow maps and of spot light shadow maps.
namespace wiwa
{
	constexpr u32 MAX_CASCADES = 4;
	constexpr u32 CASCADE_MAP_SIZE = 2048;

	struct CascadeSet
	{
		std::array<glm::mat4, MAX_CASCADES> ViewProjection{};
		std::array<float, MAX_CASCADES> SplitDepths{};   // view depth at which each cascade ends
		std::array<float, MAX_CASCADES> TexelWorldSize{}; // world size of one texel of each cascade's map
	};

	// The corners of the camera's view volume between two view depths, in world space: x * depth / P00 and y * depth / P11 for a
	// perspective camera, independent of depth for an orthographic one.
	inline std::array<glm::vec3, 8> frustum_slice_corners(const RenderCamera& camera, float sliceNear, float sliceFar)
	{
		const bool orthographic = camera.Projection[3][3] == 1.0f;
		const glm::mat4 inverseView = glm::inverse(camera.View);
		std::array<glm::vec3, 8> corners;
		for (int i = 0; i < 8; i++)
		{
			const float x = (i & 1) ? 1.0f : -1.0f;
			const float y = (i & 2) ? 1.0f : -1.0f;
			const float depth = (i & 4) ? sliceFar : sliceNear;
			const float scale = orthographic ? 1.0f : depth;
			corners[i] = glm::vec3(inverseView * glm::vec4(x * scale / camera.Projection[0][0], y * scale / camera.Projection[1][1], -depth, 1.0f));
		}
		return corners;
	}

	// Fits cascadeCount (1..4) orthographic shadow maps to the camera's view volume out to shadowDistance, split between logarithmic
	// (splitLambda 1) and uniform (0) distances. The maps are texel-snapped: moving the camera does not make the shadow edges crawl.
	inline CascadeSet compute_cascades(const RenderCamera& camera, glm::vec3 lightDirection, u32 cascadeCount, float shadowDistance, float splitLambda)
	{
		CascadeSet result;
		cascadeCount = std::clamp(cascadeCount, 1u, MAX_CASCADES);

		const float nearPlane = camera.Near;
		const float farPlane = std::max(std::min(camera.Far, shadowDistance), nearPlane * 1.01f);
		const glm::vec3 direction = glm::normalize(lightDirection);
		const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

		float previous = nearPlane;
		for (u32 i = 0; i < MAX_CASCADES; i++)
		{
			if (i >= cascadeCount)
			{
				result.SplitDepths[i] = farPlane;
				result.ViewProjection[i] = result.ViewProjection[cascadeCount - 1];
				result.TexelWorldSize[i] = result.TexelWorldSize[cascadeCount - 1];
				continue;
			}

			const float fraction = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
			const float uniform = nearPlane + (farPlane - nearPlane) * fraction;
			const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, fraction);
			const float split = i + 1 == cascadeCount ? farPlane : glm::mix(uniform, logarithmic, std::clamp(splitLambda, 0.0f, 1.0f));
			result.SplitDepths[i] = split;

			// The slice's corners and the sphere around them (rounded, so the size does not wobble when the camera turns).
			const std::array<glm::vec3, 8> corners = frustum_slice_corners(camera, previous, split);
			glm::vec3 center(0.0f);
			for (const glm::vec3& corner : corners)
			{
				center += corner / 8.0f;
			}
			float radius = 0.0f;
			for (const glm::vec3& corner : corners)
			{
				radius = std::max(radius, glm::length(corner - center));
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			// Casters up to this far toward the light are kept: anything that can cast a shadow into the slice.
			const float backDistance = radius + std::max(shadowDistance, radius * 2.0f);
			const glm::mat4 view = glm::lookAt(center - direction * backDistance, center, up);
			glm::mat4 projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, backDistance + radius);

			// Snap the map to its own texel grid: the world origin lands on a texel corner.
			const float texels = static_cast<float>(CASCADE_MAP_SIZE);
			const glm::vec4 origin = projection * view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const glm::vec2 scaled = glm::vec2(origin) * (texels * 0.5f);
			const glm::vec2 offset = (glm::round(scaled) - scaled) * (2.0f / texels);
			projection[3][0] += offset.x;
			projection[3][1] += offset.y;

			result.ViewProjection[i] = projection * view;
			result.TexelWorldSize[i] = 2.0f * radius / texels;
			previous = split;
		}
		return result;
	}

	// The shadow matrix of a spot light: a perspective view down its axis that covers the cone.
	inline glm::mat4 compute_spot_shadow_matrix(const RenderLight& light, float& tanHalfFov)
	{
		const glm::vec3 direction = glm::normalize(light.Direction);
		const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const float halfAngle = glm::radians(std::clamp(light.OuterAngle, 1.0f, 85.0f));
		tanHalfFov = std::tan(halfAngle);
		const float farPlane = std::max(light.Range, 0.2f);
		const glm::mat4 view = glm::lookAt(light.Position, light.Position + direction, up);
		const glm::mat4 projection = glm::perspectiveRH_ZO(halfAngle * 2.0f, 1.0f, std::min(0.1f, farPlane * 0.5f), farPlane);
		return projection * view;
	}
}
