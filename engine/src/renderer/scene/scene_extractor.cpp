#include <pch.h>
#include "scene_extractor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/decal_lifetime.h>
#include <scene/particle_runtime.h>
#include <scene/skin_space.h>

namespace wiwa
{
	namespace
	{
		bool same_matrix(const glm::mat4& a, const glm::mat4& b)
		{
			return std::memcmp(&a, &b, sizeof(glm::mat4)) == 0;
		}

		bool same_camera_component(const Camera& a, const Camera& b)
		{
			return a.Projection == b.Projection && a.FovY == b.FovY && a.Near == b.Near && a.Far == b.Far && a.OrthoSize == b.OrthoSize;
		}

		// The entity's forward axis (-Z of its world matrix).
		glm::vec3 forward_of(const glm::mat4& world)
		{
			const glm::vec3 axis = -glm::vec3(world[2]);
			const float length = glm::length(axis);
			return length > 1e-6f ? axis / length : glm::vec3(0.0f, 0.0f, -1.0f);
		}

		RenderLight make_light(const Light& light, const glm::mat4& world)
		{
			RenderLight result;
			result.Kind = light.Kind;
			result.Position = glm::vec3(world[3]);
			result.Direction = forward_of(world);
			result.Color = glm::vec3(light.Color);
			result.Intensity = light.Intensity;
			result.Range = light.Range;
			result.InnerAngle = light.InnerAngle;
			result.OuterAngle = light.OuterAngle;
			result.CastsShadows = light.CastsShadows;
			return result;
		}

		bool same_light(const RenderLight& a, const RenderLight& b)
		{
			return a.Kind == b.Kind && a.Position == b.Position && a.Direction == b.Direction && a.Color == b.Color && a.Intensity == b.Intensity
				&& a.Range == b.Range && a.InnerAngle == b.InnerAngle && a.OuterAngle == b.OuterAngle && a.CastsShadows == b.CastsShadows;
		}

		// age: the DecalAge of a decal that is fading or expiring while the game plays; none shows it at its full opacity (edit mode).
		RenderDecal make_decal(const Decal& decal, const glm::mat4& world, const DecalAge* age)
		{
			RenderDecal result;
			result.World = world;
			result.Material = decal.Material;
			result.Size = glm::max(decal.Size, glm::vec3(0.0f));
			result.Opacity = std::clamp(decal.Opacity, 0.0f, 1.0f) * (age != nullptr ? decal_fade(decal, age->Seconds) : 1.0f);
			result.Channels = (decal.AffectColor ? DECAL_CHANNEL_COLOR : 0u) | (decal.AffectNormal ? DECAL_CHANNEL_NORMAL : 0u)
				| (decal.AffectOrm ? DECAL_CHANNEL_ORM : 0u) | (decal.AffectEmissive ? DECAL_CHANNEL_EMISSIVE : 0u);
			result.AngleFadeStart = decal.NormalAngleFadeStart;
			result.AngleFadeEnd = decal.NormalAngleFadeEnd;
			result.EdgeFade = std::clamp(decal.EdgeFade, 0.0f, 0.5f);
			result.SortOrder = decal.SortOrder;
			return result;
		}

		// (The SpawnCount of the runtime is not part of it: those are sent as ParticleSpawns.)
		RenderParticleSystem make_particle_system(const ParticleSystem& system, const glm::mat4& world, const ParticleRuntime* runtime)
		{
			RenderParticleSystem result;
			result.World = world;
			result.Effect = system.Effect;
			result.SortBias = system.SortBias;
			result.ColorTint = system.ColorTint;
			result.SizeScale = std::max(system.SizeScale, 0.0f);
			result.RateScale = std::max(system.RateScale, 0.0f);
			result.Seed = runtime != nullptr ? runtime->Seed : 0u;
			result.ClearCount = runtime != nullptr ? runtime->ClearCount : 0u;
			return result;
		}

		bool same_particle_system(const RenderParticleSystem& a, const RenderParticleSystem& b)
		{
			return same_matrix(a.World, b.World) && a.Effect == b.Effect && a.SortBias == b.SortBias && a.ColorTint == b.ColorTint && a.SizeScale == b.SizeScale
				&& a.RateScale == b.RateScale && a.Seed == b.Seed && a.ClearCount == b.ClearCount;
		}

		bool same_decal(const RenderDecal& a, const RenderDecal& b)
		{
			return same_matrix(a.World, b.World) && a.Material == b.Material && a.Size == b.Size && a.Opacity == b.Opacity && a.Channels == b.Channels
				&& a.AngleFadeStart == b.AngleFadeStart && a.AngleFadeEnd == b.AngleFadeEnd && a.EdgeFade == b.EdgeFade && a.SortOrder == b.SortOrder;
		}
	}

	namespace
	{
		bool finite_matrix(const glm::mat4& matrix)
		{
			for (int column = 0; column < 4; column++)
			{
				for (int row = 0; row < 4; row++)
				{
					if (!std::isfinite(matrix[column][row]))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	RenderCamera make_render_camera(const Camera& camera, const glm::mat4& world, float aspect, bool* degenerate)
	{
		bool broken = false;

		glm::mat4 placement = world;
		if (!finite_matrix(placement) || std::abs(glm::determinant(placement)) < 1e-12f)
		{
			placement = glm::mat4(1.0f);
			broken = true;
		}

		float nearPlane = camera.Near;
		float farPlane = camera.Far;
		if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0f || farPlane <= nearPlane)
		{
			nearPlane = 0.1f;
			farPlane = 1000.0f;
			broken = true;
		}

		if (!std::isfinite(aspect) || aspect <= 0.0f)
		{
			aspect = 1.0f;
			broken = true;
		}

		RenderCamera result;
		result.View = glm::inverse(placement);
		result.Position = glm::vec3(placement[3]);
		result.Near = nearPlane;
		result.Far = farPlane;
		if (camera.Projection == ProjectionKind::Orthographic)
		{
			float halfHeight = camera.OrthoSize;
			if (!std::isfinite(halfHeight) || halfHeight <= 0.0f)
			{
				halfHeight = 10.0f;
				broken = true;
			}
			const float halfWidth = halfHeight * aspect;
			result.Projection = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
		}
		else
		{
			float fov = camera.FovY;
			if (!std::isfinite(fov) || fov < 1.0f || fov > 179.0f)
			{
				fov = std::isfinite(fov) ? std::clamp(fov, 1.0f, 179.0f) : 60.0f;
				broken = true;
			}
			result.Projection = glm::perspectiveRH_ZO(glm::radians(fov), aspect, nearPlane, farPlane);
		}

		if (degenerate != nullptr)
		{
			*degenerate = broken;
		}
		return result;
	}

	namespace
	{
		// The same skeleton, whichever way it is named: a scene older than the extraction still names the model's skeleton by its legacy
		// sub-asset id, the prefab names the extracted .wskel, and the catalog redirects one to the other.
		bool same_skeleton(const AssetRef& a, const AssetRef& b)
		{
			if (a == b)
			{
				return true;
			}
			const AssetManager* assets = Engine::get().get_module<AssetManager>();
			return assets != nullptr && a.valid() && b.valid() && assets->resolve(a) == assets->resolve(b);
		}

		// The Animator that drives a skinned mesh: this entity or the nearest ancestor that has one for the same skeleton.
		entt::entity find_animator(const entt::registry& registry, entt::entity mesh, const AssetRef& skeleton)
		{
			for (entt::entity entity = mesh; entity != entt::null; entity = registry.get<Hierarchy>(entity).Parent)
			{
				const Animator* animator = registry.try_get<Animator>(entity);
				if (animator != nullptr && same_skeleton(animator->Skeleton, skeleton))
				{
					return entity;
				}
			}
			return entt::null;
		}
	}

	SceneDelta SceneExtractor::extract(const Ref<Scene>& scene, float aspect, const PaletteSource& palettes)
	{
		SceneDelta delta;
		if (scene == nullptr)
		{
			return delta;
		}

		if (m_Scene.lock() != scene)
		{
			delta.Reset = true;
			m_Scene = scene;
			m_Instances.clear();
			m_Lights.clear();
			m_Decals.clear();
			m_Particles.clear();
			m_HasCamera = false;
			m_SettingsRevision = 0;
		}

		if (scene->settings_revision() != m_SettingsRevision)
		{
			m_SettingsRevision = scene->settings_revision();
			delta.Settings = scene->settings();
		}

		const u64 pass = ++m_Pass;
		entt::registry& registry = scene->registry();

		size_t instancesSeen = 0;
		for (const auto [entity, id, renderer, world] : registry.view<Id, MeshRenderer, WorldTransform>().each())
		{
			if (!renderer.Mesh.valid())
			{
				continue; // not drawable (yet); a previously sent instance is removed below
			}

			instancesSeen++;

			// a skinned mesh stands where its Animator does: the joints of the pose place the vertices, not the node (the model's import scale applies, the nodes between
				// them do not)
			glm::mat4 matrix = world.Matrix;
			float padding = 0.0f;
			std::span<const glm::vec4> palette{};
			if (const SkinBinding* binding = registry.try_get<SkinBinding>(entity))
			{
				const entt::entity animatorEntity = find_animator(registry, entity, binding->Skeleton);
				if (animatorEntity != entt::null)
				{
					if (const WorldTransform* animatorWorld = registry.try_get<WorldTransform>(animatorEntity))
					{
						matrix = animatorWorld->Matrix * model_scale_matrix(registry.get<Animator>(animatorEntity));
					}
					padding = registry.get<Animator>(animatorEntity).BoundsPadding;
					if (palettes)
					{
						palette = palettes(animatorEntity);
					}
				}
			}

			auto found = m_Instances.find(id.Guid);
			const bool known = found != m_Instances.end();
			SentInstance& sent = known ? found->second : m_Instances[id.Guid];
			sent.Seen = pass;
			if (!known || !same_matrix(sent.World, matrix) || sent.Mesh != renderer.Mesh || sent.Materials != renderer.Materials || sent.Padding != padding
				|| sent.ReceivesDecals != renderer.ReceivesDecals || sent.Static != renderer.Static)
			{
				sent.World = matrix;
				sent.Mesh = renderer.Mesh;
				sent.Materials = renderer.Materials;
				sent.Padding = padding;
				sent.ReceivesDecals = renderer.ReceivesDecals;
				sent.Static = renderer.Static;

				RenderInstance instance;
				instance.ReceivesDecals = renderer.ReceivesDecals;
				instance.Static = renderer.Static;
				instance.World = matrix;
				instance.Mesh = renderer.Mesh;
				instance.Materials = renderer.Materials;
				instance.BoundsPadding = padding;
				delta.InstanceUpserts.emplace_back(id.Guid, std::move(instance));
			}

			// the palette follows the pose, not the matrix: sent when it is not what the render thread has
			if (sent.Palette.size() != palette.size() || (!palette.empty() && std::memcmp(sent.Palette.data(), palette.data(), palette.size_bytes()) != 0))
			{
				sent.Palette.assign(palette.begin(), palette.end());
				delta.PaletteUpdates.emplace_back(id.Guid, sent.Palette);
			}
		}

		if (m_Instances.size() != instancesSeen)
		{
			for (auto it = m_Instances.begin(); it != m_Instances.end();)
			{
				if (it->second.Seen != pass)
				{
					delta.InstanceRemovals.push_back(it->first);
					it = m_Instances.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		size_t lightsSeen = 0;
		for (const auto [entity, id, light, world] : registry.view<Id, Light, WorldTransform>().each())
		{
			lightsSeen++;
			RenderLight current = make_light(light, world.Matrix);
			auto found = m_Lights.find(id.Guid);
			if (found != m_Lights.end() && same_light(found->second.Light, current))
			{
				found->second.Seen = pass;
				continue;
			}

			SentLight& sent = m_Lights[id.Guid];
			sent.Light = current;
			sent.Seen = pass;
			delta.LightUpserts.emplace_back(id.Guid, std::move(current));
		}

		if (m_Lights.size() != lightsSeen)
		{
			for (auto it = m_Lights.begin(); it != m_Lights.end();)
			{
				if (it->second.Seen != pass)
				{
					delta.LightRemovals.push_back(it->first);
					it = m_Lights.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		size_t decalsSeen = 0;
		for (const auto [entity, id, decal, world] : registry.view<Id, Decal, WorldTransform>().each())
		{
			decalsSeen++;
			RenderDecal current = make_decal(decal, world.Matrix, registry.try_get<DecalAge>(entity));
			auto found = m_Decals.find(id.Guid);
			if (found != m_Decals.end() && same_decal(found->second.Decal, current))
			{
				found->second.Seen = pass;
				continue;
			}

			SentDecal& sent = m_Decals[id.Guid];
			sent.Decal = current;
			sent.Seen = pass;
			delta.DecalUpserts.emplace_back(id.Guid, std::move(current));
		}

		if (m_Decals.size() != decalsSeen)
		{
			for (auto it = m_Decals.begin(); it != m_Decals.end();)
			{
				if (it->second.Seen != pass)
				{
					delta.DecalRemovals.push_back(it->first);
					it = m_Decals.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		size_t particlesSeen = 0;
		for (const auto [entity, system, id, world] : registry.view<ParticleSystem, Id, WorldTransform>().each())
		{
			if (!system.Effect.valid())
			{
				continue; // nothing to play (yet); a system sent before is removed below
			}

			particlesSeen++;
			ParticleRuntime* runtime = registry.try_get<ParticleRuntime>(entity);
			RenderParticleSystem current = make_particle_system(system, world.Matrix, runtime);
			auto found = m_Particles.find(id.Guid);
			if (found != m_Particles.end() && same_particle_system(found->second.System, current))
			{
				found->second.Seen = pass;
			}
			else
			{
				SentParticles& sent = m_Particles[id.Guid];
				sent.System = current;
				sent.Seen = pass;
				delta.ParticleUpserts.emplace_back(id.Guid, std::move(current));
			}

			if (runtime != nullptr)
			{
				const bool any = std::any_of(runtime->SpawnCount.begin(), runtime->SpawnCount.end(), [](u32 count) { return count != 0; });
				if (any)
				{
					delta.ParticleSpawns.emplace_back(id.Guid, runtime->SpawnCount);
					runtime->SpawnCount.fill(0); // taken: a paused game does not tick, and must not send the same spawns again
				}
			}
		}

		if (m_Particles.size() != particlesSeen)
		{
			for (auto it = m_Particles.begin(); it != m_Particles.end();)
			{
				if (it->second.Seen != pass)
				{
					delta.ParticleRemovals.push_back(it->first);
					it = m_Particles.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		// The first camera marked Primary.
		bool found = false;
		for (const auto [entity, id, camera, world] : registry.view<Id, Camera, WorldTransform>().each())
		{
			if (!camera.Primary)
			{
				continue;
			}

			found = true;
			bool degenerate = false;
			const RenderCamera current = make_render_camera(camera, world.Matrix, aspect, &degenerate);
			if (degenerate && !m_WarnedDegenerate)
			{
				WI_LOG(Warning, "Renderer", "the primary camera has values that cannot make a camera (near/far, field of view, transform): defaults are used");
			}
			m_WarnedDegenerate = degenerate;
			if (!m_HasCamera || m_CameraId != id.Guid || m_CameraAspect != aspect || !same_matrix(m_Camera.View, current.View)
				|| !same_camera_component(m_CameraComponent, camera))
			{
				m_HasCamera = true;
				m_CameraId = id.Guid;
				m_CameraAspect = aspect;
				m_CameraComponent = camera;
				m_Camera = current;
				delta.Camera = current;
			}
			break;
		}

		if (!found && m_HasCamera)
		{
			m_HasCamera = false;
			delta.CameraRemoved = true;
		}

		return delta;
	}
}
