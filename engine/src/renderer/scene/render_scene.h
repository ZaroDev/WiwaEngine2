#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>
#include <scene/components.h>
#include <scene/entity_guid.h>

namespace wiwa
{
	// The render thread's copy of what it draws, built from the deltas the main thread extracts from the ECS. It holds data only: no
	// registry, no handles. The meshes and materials are AssetRefs that the GpuResourceCache turns into GPU objects.

	struct RenderInstance
	{
		bool Alive = false;
		glm::mat4 World{ 1.0f };
		AssetRef Mesh{};
		std::vector<AssetRef> Materials{}; // overrides per primitive; empty means the mesh's own materials
	};

	struct RenderLight
	{
		bool Alive = false;
		LightKind Kind = LightKind::Point;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f }; // where the light points (directional and spot): the entity's forward axis
		glm::vec3 Color{ 1.0f };
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerAngle = 20.0f; // degrees
		float OuterAngle = 30.0f;
		bool CastsShadows = false;
	};

	struct RenderCamera
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 Position{ 0.0f };
		float Near = 0.1f;
		float Far = 1000.0f;
	};

	// Everything that changed on the main thread since the last frame.
	struct SceneDelta
	{
		bool Reset = false; // the active scene is a different one: drop everything first, then apply what follows
		std::vector<std::pair<EntityGuid, RenderInstance>> InstanceUpserts{};
		std::vector<EntityGuid> InstanceRemovals{};
		std::vector<std::pair<EntityGuid, RenderLight>> LightUpserts{};
		std::vector<EntityGuid> LightRemovals{};
		std::optional<RenderCamera> Camera{}; // the primary camera, when it changed
		bool CameraRemoved = false;

		bool empty() const
		{
			return !Reset && InstanceUpserts.empty() && InstanceRemovals.empty() && LightUpserts.empty() && LightRemovals.empty()
				&& !Camera && !CameraRemoved;
		}
	};

	// Render thread only. Instances and lights live in slots that stay put while they exist; a removed slot is reused.
	class RenderScene
	{
	public:
		void apply(SceneDelta&& delta);

		// How many instances / lights exist, and how many slots there are (some may be dead: check Alive).
		size_t instance_count() const { return m_InstanceCount; }
		size_t light_count() const { return m_LightCount; }
		const std::vector<RenderInstance>& instances() const { return m_Instances; }
		const std::vector<RenderLight>& lights() const { return m_Lights; }
		std::optional<size_t> find_instance(EntityGuid guid) const;
		std::optional<size_t> find_light(EntityGuid guid) const;

		// The scene's primary camera.
		const std::optional<RenderCamera>& camera() const { return m_Camera; }

		// Counts up on every apply that changed something.
		u64 revision() const { return m_Revision; }

	private:
		template<typename T>
		struct Slots
		{
			std::vector<T>* Items = nullptr;
			std::unordered_map<EntityGuid, size_t>* Index = nullptr;
			std::vector<size_t>* Free = nullptr;
			size_t* Count = nullptr;
		};

		template<typename T>
		static void upsert(Slots<T> slots, EntityGuid guid, T&& value);
		template<typename T>
		static void remove(Slots<T> slots, EntityGuid guid);

		std::vector<RenderInstance> m_Instances{};
		std::unordered_map<EntityGuid, size_t> m_InstanceIndex{};
		std::vector<size_t> m_FreeInstances{};
		size_t m_InstanceCount = 0;

		std::vector<RenderLight> m_Lights{};
		std::unordered_map<EntityGuid, size_t> m_LightIndex{};
		std::vector<size_t> m_FreeLights{};
		size_t m_LightCount = 0;

		std::optional<RenderCamera> m_Camera{};
		u64 m_Revision = 0;
	};
}
