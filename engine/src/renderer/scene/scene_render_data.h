#pragma once

#include <vector>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "../resources/gpu_resource_cache.h"
#include "../viewport_target.h"
#include "render_scene.h"

namespace wiwa
{
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
