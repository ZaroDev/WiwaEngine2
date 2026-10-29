#pragma once

#include <chrono>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "../resources/gpu_resource_cache.h"
#include "../viewport_target.h"
#include "render_scene.h"

namespace wiwa
{
	// The cluster grid lights are sorted into (mirrored in scene_common.hlsli).
	constexpr u32 CLUSTERS_X = 16;
	constexpr u32 CLUSTERS_Y = 9;
	constexpr u32 CLUSTERS_Z = 24;
	constexpr u32 CLUSTER_COUNT = CLUSTERS_X * CLUSTERS_Y * CLUSTERS_Z;
	constexpr u32 MAX_LIGHTS_PER_CLUSTER = 64;
	constexpr u32 MAX_LIGHTS = 1024;

	// One point or spot light as the shaders read it (GpuLight in scene_common.hlsli).
	struct GpuLight
	{
		glm::vec3 Position{ 0.0f };
		float Range = 0.0f;
		glm::vec3 Color{ 0.0f };
		float Intensity = 0.0f;
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
		float InnerCos = 1.0f;
		float OuterCos = 0.0f;
		u32 Type = 0; // 0 point, 1 spot
		u32 ShadowIndex = 0;
		u32 Padding = 0;
	};
	static_assert(sizeof(GpuLight) == 64);

	// One indexed draw of a mesh primitive.
	struct DrawItem
	{
		u32 Instance = 0;   // slot in the scene's instance array (and the instance buffer)
		u32 Material = 0;   // slot in the material buffer
		u32 Page = 0;       // which shared vertex / index buffer pair
		u32 FirstIndex = 0;
		u32 IndexCount = 0;
		i32 BaseVertex = 0;
		bool DoubleSided = false;
		float Depth = 0.0f; // view-space distance of the primitive's bounds center (for sorting blended draws)
	};

	// One scene instance as the shaders read it (SceneInstance in scene_common.hlsli): the world matrix and the matrix for normals,
	// each as four columns.
	struct GpuInstance
	{
		glm::mat4 World{ 1.0f };
		glm::mat4 Normal{ 1.0f };
	};
	static_assert(sizeof(GpuInstance) == 128);

	// The per-frame constant buffer (FrameBuffer in scene_common.hlsli).
	struct FrameConstants
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };
		glm::vec4 CameraPosition{ 0.0f }; // xyz, near
		glm::vec4 CameraParams{ 0.0f };   // x = far
		glm::vec4 SunDirection{ 0.0f, -1.0f, 0.0f, 0.0f }; // xyz: the way the light travels; w: 1 when there is a sun
		glm::vec4 SunColor{ 0.0f };       // rgb * intensity
		glm::vec4 SkyColor{ 0.0f };       // rgb * ambient intensity
		glm::vec4 GroundColor{ 0.0f };
		glm::vec4 ViewportSize{ 1.0f };   // width, height, 1 / width, 1 / height
		glm::vec4 ClusterParams{ 0.0f };  // x = slice scale, y = slice bias, z = 1 when the camera is orthographic
		glm::uvec4 LightInfo{ 0 };        // x = number of lights
	};
	static_assert(sizeof(FrameConstants) % 16 == 0);

	// What the scene passes share for one frame: the draw lists (culled against the camera), the buffers they read and the binding
	// set that carries them. Built on the render thread once per frame, after the GPU resource cache has been updated.
	class SceneRenderData
	{
	public:
		bool initialize(nvrhi::IDevice* device);
		void shutdown();

		// Culls the scene against the effective camera and fills the draw lists and the constants. Without a viewport target there
		// is nothing to draw into and the data is inactive.
		void build(RenderScene& scene, GpuResourceCache& cache, ViewportTarget& viewport);
		// Writes the instance buffer (when the scene changed) and the constants; records into the frame's command list.
		void write(nvrhi::ICommandList* commandList, GpuResourceCache& cache);

		// There is a viewport target to draw into (the passes draw the background even when there is nothing else).
		bool active() const { return m_Active; }
		bool has_camera() const { return m_HasCamera; }
		ViewportTarget* viewport() const { return m_Viewport; }
		GpuResourceCache* cache() const { return m_Cache; }
		const RenderScene* scene() const { return m_Scene; }
		const SceneSettings& settings() const { return m_Settings; }
		const FrameConstants& constants() const { return m_Constants; }

		const std::vector<DrawItem>& opaque() const { return m_Opaque; }   // sorted by cull mode, page and material
		const std::vector<DrawItem>& masked() const { return m_Masked; }   // alpha-tested, same order
		const std::vector<DrawItem>& blended() const { return m_Blended; } // back to front

		nvrhi::IBindingLayout* frame_layout() const { return m_FrameLayout; }
		nvrhi::IBindingSet* frame_bindings() const { return m_FrameBindings; }

		// Draw counts of the last build, for the stats.
		u32 visible_instances() const { return m_VisibleInstances; }

		// The point and spot lights of this frame (at most MAX_LIGHTS; the rest are counted in dropped_lights()).
		const std::vector<GpuLight>& lights() const { return m_Lights; }
		u32 dropped_lights() const { return m_DroppedLights; }

		nvrhi::IBuffer* constant_buffer() const { return m_ConstantBuffer; }
		nvrhi::IBuffer* light_buffer() const { return m_LightBuffer; }
		nvrhi::IBuffer* cluster_counts() const { return m_ClusterCounts; }   // CLUSTER_COUNT uints, written by the light culling pass
		nvrhi::IBuffer* cluster_indices() const { return m_ClusterIndices; } // CLUSTER_COUNT * MAX_LIGHTS_PER_CLUSTER uints

	private:
		nvrhi::IDevice* m_Device = nullptr;
		bool m_Active = false;
		bool m_HasCamera = false;
		ViewportTarget* m_Viewport = nullptr;
		GpuResourceCache* m_Cache = nullptr;
		const RenderScene* m_Scene = nullptr;
		SceneSettings m_Settings{};
		FrameConstants m_Constants{};
		std::vector<DrawItem> m_Opaque{};
		std::vector<DrawItem> m_Masked{};
		std::vector<DrawItem> m_Blended{};
		u32 m_VisibleInstances = 0;
		std::vector<GpuLight> m_Lights{};
		u32 m_DroppedLights = 0;
		std::chrono::steady_clock::time_point m_LastLightWarning{};

		nvrhi::BufferHandle m_LightBuffer{};
		nvrhi::BufferHandle m_ClusterCounts{};
		nvrhi::BufferHandle m_ClusterIndices{};
		nvrhi::BindingLayoutHandle m_FrameLayout{};
		nvrhi::BindingSetHandle m_FrameBindings{};
		nvrhi::BufferHandle m_ConstantBuffer{};
		nvrhi::BufferHandle m_InstanceBuffer{};
		u32 m_InstanceCapacity = 0;
		u64 m_WrittenRevision = ~0ull;
		const nvrhi::IBuffer* m_BoundInstanceBuffer = nullptr;
		const nvrhi::IBuffer* m_BoundMaterialBuffer = nullptr;
		std::vector<GpuInstance> m_InstanceScratch{};
	};
}
