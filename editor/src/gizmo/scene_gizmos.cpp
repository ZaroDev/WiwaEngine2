#include <editor_common.h>
#include "scene_gizmos.h"

#include <cmath>

#include <math/gizmo_math.h>
#include <renderer/scene/scene_extractor.h>
#include <core/engine.h>
#include <scene/components.h>
#include <scene/particle_module.h>
#include <scene/particle_runtime.h>

namespace wiwa
{
	namespace
	{
		constexpr float MARKER_PIXELS = 7.0f;   // half the size of the cross that marks a light or camera
		constexpr float ICON_PIXELS = 12.0f;    // the radius of the click target of a marker
		constexpr float AXES_PIXELS = 70.0f;    // the length of the world axes at the origin
		constexpr float ARROW_PIXELS = 60.0f;   // the length of a directional light's arrow
		constexpr float DISPLAY_FAR = 5.0f;     // a camera's frustum is shown no deeper than this (its far plane is usually far away)
		constexpr float DISPLAY_ASPECT = 16.0f / 9.0f;
		constexpr float MAX_CONE_DEGREES = 89.0f;
		const glm::vec4 LIGHT_COLOR(1.0f, 0.85f, 0.3f, 1.0f);
		const glm::vec4 CAMERA_COLOR(0.6f, 0.85f, 1.0f, 1.0f);
		const glm::vec4 DECAL_COLOR(1.0f, 0.5f, 0.45f, 1.0f);
		const glm::vec4 PARTICLE_COLOR(1.0f, 0.75f, 0.3f, 1.0f);
		const glm::vec4 BOUNDS_COLOR(0.5f, 1.0f, 0.5f, 1.0f);
		const glm::vec4 GRID_COLOR(0.6f, 0.6f, 0.6f, 0.6f);
		constexpr i32 GRID_HALF_COUNT = 50;     // lines on each side of the camera, per level

		// One level of the ground grid, centered under the camera and snapped to its own cells so it never swims; lines fade out toward the edge.
		void ground_grid(DebugDraw& draw, const glm::vec3& camera, float cell, float level)
		{
			if (level <= 0.0f)
			{
				return;
			}
			const float cx = std::round(camera.x / cell) * cell;
			const float cz = std::round(camera.z / cell) * cell;
			const float extent = cell * static_cast<float>(GRID_HALF_COUNT);
			for (i32 i = -GRID_HALF_COUNT; i <= GRID_HALF_COUNT; i++)
			{
				const float t = 1.0f - static_cast<float>(std::abs(i)) / static_cast<float>(GRID_HALF_COUNT + 1);
				const glm::vec4 color(GRID_COLOR.x, GRID_COLOR.y, GRID_COLOR.z, GRID_COLOR.w * level * t * t);
				const float offset = cell * static_cast<float>(i);
				draw.line(glm::vec3(cx + offset, 0.0f, cz - extent), glm::vec3(cx + offset, 0.0f, cz + extent), color, DebugDepth::Faded, 1.0f);
				draw.line(glm::vec3(cx - extent, 0.0f, cz + offset), glm::vec3(cx + extent, 0.0f, cz + offset), color, DebugDepth::Faded, 1.0f);
			}
		}

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

		if (settings.Grid)
		{
			// Infinite in appearance: the cell grows by a power of ten with the camera's height, the finer level fading out as the coarser one takes over.
			const glm::vec3 camera(glm::inverse(view.View)[3]);
			if (finite(camera))
			{
				const float decade = std::log10(std::max(std::abs(camera.y), 1.0f));
				const float cell = std::pow(10.0f, std::floor(decade));
				ground_grid(draw, camera, cell, 1.0f - (decade - std::floor(decade)));
				ground_grid(draw, camera, cell * 10.0f, 1.0f);
			}
		}

		if (settings.Axes)
		{
			draw.axes(glm::mat4(1.0f), size_at(glm::vec3(0.0f), AXES_PIXELS), DebugDepth::Faded, 2.0f);
		}

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

		if (settings.Decals)
		{
			for (auto [entity, decal, world, id] : registry.view<Decal, WorldTransform, Id>().each())
			{
				const glm::vec3 position(world.Matrix[3]);
				if (!finite(position))
				{
					continue;
				}

				marker(position, DECAL_COLOR);
				draw.pick_icon(id.Guid, position, ICON_PIXELS);
				if (entity == selected)
				{
					// the box the decal projects into, and the way it projects (local -Z): the arrow starts on the box's back face and points through it
					draw.box(world.Matrix, glm::max(decal.Size, glm::vec3(0.0f)) * 0.5f, DECAL_COLOR, DebugDepth::Tested, 1.0f);
					const glm::vec3 projection = forward_of(world.Matrix);
					const float depth = decal.Size.z * glm::length(glm::vec3(world.Matrix[2]));
					draw.arrow(position - projection * depth * 0.5f, position + projection * depth * 0.5f, DECAL_COLOR, DebugDepth::AlwaysOnTop, 2.0f);
				}
			}
		}

		if (settings.Particles)
		{
			ParticleModule* particles = Engine::get().get_module<ParticleModule>();
			for (auto [entity, system, world, id] : registry.view<ParticleSystem, WorldTransform, Id>().each())
			{
				const glm::vec3 position(world.Matrix[3]);
				if (!finite(position))
				{
					continue;
				}

				marker(position, PARTICLE_COLOR);
				draw.pick_icon(id.Guid, position, ICON_PIXELS);
				const ParticleEffectData* effect = entity == selected && particles != nullptr ? particles->effect(system.Effect) : nullptr;
				if (effect == nullptr)
				{
					continue;
				}

				// the shape every emitter spawns from (in the system's space), and a faint sphere for how far the fastest particles get in their longest life
				const glm::vec3 axisY(world.Matrix[1]);
				const float scaleY = glm::length(axisY);
				const float scale = std::max({ glm::length(glm::vec3(world.Matrix[0])), scaleY, glm::length(glm::vec3(world.Matrix[2])) });
				float reach = 0.0f;
				for (const ParticleEmitterData& emitter : effect->Emitters)
				{
					switch (emitter.Shape)
					{
					case ParticleShape::Point:
						break;
					case ParticleShape::Sphere:
						draw.sphere(position, std::max(emitter.ShapeSize.x, 0.0f) * scale, PARTICLE_COLOR, DebugDepth::Tested, 1.0f);
						break;
					case ParticleShape::Cone:
					{
						const glm::vec3 up = scaleY > 1e-6f ? axisY / scaleY : glm::vec3(0.0f, 1.0f, 0.0f);
						draw.circle(position, up, std::max(emitter.ShapeSize.x, 0.0f) * scale, PARTICLE_COLOR, DebugDepth::Tested, 1.0f);
						draw.cone(position, up, std::max(scaleY, 0.1f), glm::radians(std::clamp(emitter.ConeAngle, 0.0f, MAX_CONE_DEGREES)), PARTICLE_COLOR, DebugDepth::Tested, 1.0f);
						break;
					}
					case ParticleShape::Box:
						draw.box(world.Matrix, glm::max(emitter.ShapeSize, glm::vec3(0.0f)) * 0.5f, PARTICLE_COLOR, DebugDepth::Tested, 1.0f);
						break;
					}
					const float life = particle_value_max(emitter.Lifetime);
					const float travel = std::max(particle_value_max(emitter.StartSpeed), 0.0f) * life + 0.5f * glm::length(emitter.Gravity) * life * life;
					reach = std::max(reach, (travel + std::max(emitter.ShapeSize.x, 0.0f)) * scale);
				}
				if (reach > 0.0f && std::isfinite(reach))
				{
					draw.sphere(position, reach, glm::vec4(PARTICLE_COLOR.x, PARTICLE_COLOR.y, PARTICLE_COLOR.z, 0.25f), DebugDepth::Faded, 1.0f);
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
