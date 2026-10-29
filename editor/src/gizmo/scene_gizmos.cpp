#include <editor_common.h>
#include "scene_gizmos.h"

#include <cmath>

#include <math/gizmo_math.h>
#include <renderer/scene/scene_extractor.h>
#include <scene/components.h>

namespace wiwa
{
	namespace
	{
		constexpr float MARKER_PIXELS = 7.0f;   // half the size of the cross that marks a light or camera
		constexpr float ICON_PIXELS = 12.0f;    // the radius of the click target of a marker
		constexpr float ARROW_PIXELS = 60.0f;   // the length of a directional light's arrow
		constexpr float DISPLAY_FAR = 5.0f;     // a camera's frustum is shown no deeper than this (its far plane is usually far away)
		constexpr float DISPLAY_ASPECT = 16.0f / 9.0f;
		constexpr float MAX_CONE_DEGREES = 89.0f;
		const glm::vec4 LIGHT_COLOR(1.0f, 0.85f, 0.3f, 1.0f);
		const glm::vec4 CAMERA_COLOR(0.6f, 0.85f, 1.0f, 1.0f);
		const glm::vec4 BOUNDS_COLOR(0.5f, 1.0f, 0.5f, 1.0f);

		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		// The entity's forward axis, -Z of its world matrix: what the renderer treats as the direction of a light.
		glm::vec3 forward_of(const glm::mat4& world)
		{
			const glm::vec3 forward = -glm::vec3(world[2]);
			const float length = glm::length(forward);
			return std::isfinite(length) && length > 1e-6f ? forward / length : glm::vec3(0.0f, 0.0f, -1.0f);
		}
	}

	std::optional<SceneGizmos::Bounds> SceneGizmos::bounds_of(const AssetRef& mesh)
	{
		if (m_Assets == nullptr)
		{
			return m_Lookup ? m_Lookup(mesh) : std::nullopt;
		}
		if (!mesh.valid())
		{
			return std::nullopt;
		}

		const AssetGuid key = mesh.resolved();
		auto found = m_Meshes.find(key);
		if (found == m_Meshes.end())
		{
			found = m_Meshes.emplace(key, m_Assets->load<MeshData>(mesh)).first;
		}
		const MeshData* data = found->second.get();
		if (data == nullptr)
		{
			return std::nullopt;
		}
		return Bounds(data->BoundsMin, data->BoundsMax);
	}

	void SceneGizmos::draw(DebugDraw& draw, Scene& scene, entt::entity selected, const GizmoSettings& settings, const SceneGizmoView& view)
	{
		entt::registry& registry = scene.registry();
		const bool hasSelection = registry.valid(selected);

		auto size_at = [&](const glm::vec3& position, float pixels)
		{
			return gizmo::screen_constant_scale(position, view.View, view.Projection, pixels, view.ViewportHeight);
		};

		// A cross of three lines through the position: where a light or a camera is.
		auto marker = [&](const glm::vec3& position, const glm::vec4& color)
		{
			const float r = size_at(position, MARKER_PIXELS);
			draw.line(position - glm::vec3(r, 0.0f, 0.0f), position + glm::vec3(r, 0.0f, 0.0f), color, DebugDepth::AlwaysOnTop, 2.0f);
			draw.line(position - glm::vec3(0.0f, r, 0.0f), position + glm::vec3(0.0f, r, 0.0f), color, DebugDepth::AlwaysOnTop, 2.0f);
			draw.line(position - glm::vec3(0.0f, 0.0f, r), position + glm::vec3(0.0f, 0.0f, r), color, DebugDepth::AlwaysOnTop, 2.0f);
		};

		if (settings.Lights)
		{
			for (auto [entity, light, world, id] : registry.view<Light, WorldTransform, Id>().each())
			{
				const glm::vec3 position(world.Matrix[3]);
				if (!finite(position))
				{
					continue;
				}

				marker(position, LIGHT_COLOR);
				draw.pick_icon(id.Guid, position, ICON_PIXELS);
				const glm::vec3 forward = forward_of(world.Matrix);

				if (light.Kind == LightKind::Directional)
				{
					draw.arrow(position, position + forward * size_at(position, ARROW_PIXELS), LIGHT_COLOR, DebugDepth::AlwaysOnTop, 2.0f);
				}
				else if (entity == selected && light.Kind == LightKind::Point)
				{
					draw.sphere(position, light.Range, LIGHT_COLOR, DebugDepth::Tested, 1.0f);
				}
				else if (entity == selected && light.Kind == LightKind::Spot)
				{
					draw.cone(position, forward, light.Range, glm::radians(std::min(light.OuterAngle, MAX_CONE_DEGREES)), LIGHT_COLOR, DebugDepth::Tested, 1.0f);
				}
			}
		}

		if (settings.Cameras)
		{
			for (auto [entity, camera, world, id] : registry.view<Camera, WorldTransform, Id>().each())
			{
				const glm::vec3 position(world.Matrix[3]);
				if (!finite(position))
				{
					continue;
				}

				marker(position, CAMERA_COLOR);
				draw.pick_icon(id.Guid, position, ICON_PIXELS);

				Camera shown = camera;
				shown.Far = std::min(camera.Far, std::max(camera.Near * 2.0f, DISPLAY_FAR));
				const RenderCamera render = make_render_camera(shown, world.Matrix, DISPLAY_ASPECT);
				draw.frustum(render.Projection * render.View, CAMERA_COLOR, DebugDepth::Tested, 1.0f);
			}
		}

		if (settings.Bounds && hasSelection && registry.all_of<MeshRenderer, WorldTransform>(selected))
		{
			if (const std::optional<Bounds> local = bounds_of(registry.get<MeshRenderer>(selected).Mesh))
			{
				const glm::mat4& world = registry.get<WorldTransform>(selected).Matrix;
				glm::vec3 low(std::numeric_limits<float>::max());
				glm::vec3 high(std::numeric_limits<float>::lowest());
				for (u32 i = 0; i < 8; i++)
				{
					const glm::vec3 corner((i & 1) ? local->second.x : local->first.x, (i & 2) ? local->second.y : local->first.y, (i & 4) ? local->second.z : local->first.z);
					const glm::vec3 moved = glm::vec3(world * glm::vec4(corner, 1.0f));
					low = glm::min(low, moved);
					high = glm::max(high, moved);
				}
				if (finite(low) && finite(high))
				{
					draw.box(low, high, BOUNDS_COLOR, DebugDepth::Tested, 1.0f);
				}
			}
		}
	}
}
