#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>
#include <scene/components.h>
#include <scene/entity_guid.h>
#include <scene/scene_settings.h>

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

	// How the selected entity is outlined. The color is in display (sRGB) space, as the LDR image holds it.
	struct SelectionOutlineStyle
	{
		glm::vec3 Color{ 1.0f, 0.55f, 0.1f };
		float WidthPx = 2.0f;        // 1..8
		float OccludedAlpha = 0.35f; // strength of the parts that something in front of the selection hides
		bool Enabled = true;
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
		std::optional<SceneSettings> Settings{}; // when they changed (always with a Reset)

		bool empty() const
		{
			return !Reset && InstanceUpserts.empty() && InstanceRemovals.empty() && LightUpserts.empty() && LightRemovals.empty()
				&& !Camera && !CameraRemoved && !Settings;
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

		// A camera that replaces the scene's (the editor's fly camera); an empty one gives the scene's camera back. A Reset does not
		// clear it.
		void set_camera_override(std::optional<RenderCamera> camera) { m_CameraOverride = camera; }
		// What the frame is drawn with: the override if there is one, else the scene's camera.
		const std::optional<RenderCamera>& effective_camera() const { return m_CameraOverride ? m_CameraOverride : m_Camera; }

		// The entity whose meshes get the outline (the editor's selection). Not part of the scene: a Reset does not clear it, and a guid
		// that is not an instance of the scene (a light, a removed entity) outlines nothing.
		void set_selection(std::optional<EntityGuid> guid) { m_Selection = guid; }
		const std::optional<EntityGuid>& selection() const { return m_Selection; }
		void set_outline_style(const SelectionOutlineStyle& style) { m_OutlineStyle = style; }
		const SelectionOutlineStyle& outline_style() const { return m_OutlineStyle; }

		const SceneSettings& settings() const { return m_Settings; }
		void set_settings(const SceneSettings& settings) { m_Settings = settings; }

		// Counts up on every apply that changed something.
		u64 revision() const { return m_Revision; }
		// Counts up when another scene becomes the active one (a Reset).
		u32 reset_count() const { return m_ResetCount; }

	private:
		u32 m_ResetCount = 0;
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
		std::optional<RenderCamera> m_CameraOverride{};
		std::optional<EntityGuid> m_Selection{};
		SelectionOutlineStyle m_OutlineStyle{};
		SceneSettings m_Settings{};
		u64 m_Revision = 0;
	};
}
