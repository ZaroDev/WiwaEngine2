#pragma once

#include <chrono>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "../resources/gpu_resource_cache.h"
#include "../environment/environment_cache.h"
#include "../render_quality.h"
#include "../viewport_target.h"
#include "render_scene.h"

namespace wiwa
{
	class Frustum;
	// The cluster grid lights are sorted into (mirrored in scene_common.hlsli).
	constexpr u32 CLUSTERS_X = 16;
	constexpr u32 CLUSTERS_Y = 9;
	constexpr u32 CLUSTERS_Z = 24;
	constexpr u32 CLUSTER_COUNT = CLUSTERS_X * CLUSTERS_Y * CLUSTERS_Z;
	constexpr u32 MAX_LIGHTS_PER_CLUSTER = 64;
	constexpr u32 MAX_LIGHTS = 1024;

	constexpr u32 MAX_SPOT_SHADOWS = 4;
	constexpr u32 SPOT_TILE_SIZE = 1024;
	constexpr u32 SPOT_ATLAS_SIZE = 4096;

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
		glm::mat4 ShadowMatrices[8]{};    // 0..3 the sun's cascades, 4..7 the shadowed spot lights
		glm::vec4 CascadeSplits{ 0.0f };  // view depth at which each cascade ends
		glm::vec4 CascadeTexels{ 0.0f };  // world size of a texel of each cascade
		glm::vec4 ShadowParams{ 0.0f };   // x = 1 when the sun casts shadows, y = cascade count, z = depth bias, w = normal bias (texels)
		glm::vec4 ShadowParams2{ 0.0f };  // x = spot depth bias
		glm::vec4 SpotShadowRects[4]{};   // atlas tile of each shadowed spot light: offset xy, scale zw
		glm::vec4 SpotShadowInfo[4]{};    // x = tan(half the cone angle)
		glm::vec4 EnvSH[9]{};             // the environment's diffuse light (rgb in xyz)
		glm::vec4 EnvParams{ 0.0f };      // x = 1 when an environment lights the scene, y = highest mip, z = brightness * ambient intensity, w = 1 when the BRDF table exists
		glm::vec4 EnvRotation{ 1.0f, 0.0f, 1.0f, 0.0f }; // cos, sin of the rotation about +Y, the brightness of the sky seen
		glm::mat4 InverseViewProjection{ 1.0f };
		glm::vec4 AtmosphereA{ 1.0f, 1.0f, 1.0f, 0.8f }; // Rayleigh scale, Mie scale, ozone scale, Mie anisotropy
		glm::vec4 AtmosphereB{ 0.6f, 0.05f, 24.0f, 8.0f }; // multiple scattering, observer altitude (km), steps along the view ray, steps towards the sun
		glm::vec4 AtmosphereGround{ 0.05f, 0.08f, 0.1f, 120.0f }; // ground albedo, sun disc boost
		glm::vec4 AtmosphereSun{ 0.0f, 1.0f, 0.0f, 0.0f };        // towards the sun
		glm::vec4 AtmosphereIrradiance{ 3.4f, 3.4f, 3.4f, 3.5f }; // the sun outside the atmosphere, sky brightness
	};
	static_assert(sizeof(FrameConstants) % 16 == 0);

	// One shadow map to render this frame and the casters in it.
	struct ShadowView
	{
		glm::mat4 ViewProjection{ 1.0f };
		u32 MatrixIndex = 0;  // index into FrameConstants::ShadowMatrices, also what the draws carry as their "material"
		bool Atlas = false;   // a tile of the spot atlas, else a slice of the cascade array
		u32 Slice = 0;        // cascade, or tile
		std::vector<DrawItem> Items{};
	};

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
		// The scene has ambient occlusion on this frame: the AO pass writes ViewportTarget::ao() and the forward pass reads it (else a
		// white stand-in).
		bool ao_active() const { return m_AoActive; }
		// The forward pass reads the occlusion through a layout of its own (one SRV, t15): the frame layout's registers already span
		// 1..9, and NVRHI rejects layouts whose register ranges overlap those of the material layout (t10..t13).
		nvrhi::IBindingLayout* ao_layout() const { return m_AoLayout; }
		nvrhi::IBindingSet* ao_bindings() const { return m_AoBindings; }
		// An environment (a file's, ready) lights the scene this frame.
		bool has_environment() const { return m_FrameEnvironment != nullptr; }
		// What the sky pass draws behind the scene this frame.
		enum class SkyKind { None, Environment, Atmosphere };
		SkyKind sky_kind() const { return m_SkyKind; }
		ViewportTarget* viewport() const { return m_Viewport; }
		GpuResourceCache* cache() const { return m_Cache; }
		const RenderScene* scene() const { return m_Scene; }
		const SceneSettings& settings() const { return m_Settings; }
		// Where the environments of the settings come from; without one the scene is lit by the hemisphere ambient.
		void set_environments(EnvironmentCache* environments) { m_Environments = environments; }

		// What the project decides about shadows (the Renderer sets it; checks change it).
		RenderQuality& quality() { return m_Quality; }
		const RenderQuality& quality() const { return m_Quality; }
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

		// Off: every instance is drawn and every caster is rendered, whatever the camera sees (for comparing against the culled frame).
		void set_culling_enabled(bool enabled) { m_Culling = enabled; m_ClusterCulling = enabled; }
		// Off: the forward shader tests every light instead of its cluster's list.

		// The shadow maps to draw this frame; they are made when the first view needs them (and are 1x1 stand-ins until then).
		const std::vector<ShadowView>& shadow_views() const { return m_ShadowViews; }
		nvrhi::ITexture* cascade_map() const { return m_CascadeMap; }
		nvrhi::ITexture* spot_atlas() const { return m_SpotAtlas; }
		u32 shadow_generation() const { return m_ShadowGeneration; } // changes when a shadow map is made
		nvrhi::IBuffer* instance_buffer() const { return m_InstanceBuffer; }

		nvrhi::IBuffer* constant_buffer() const { return m_ConstantBuffer; }
		nvrhi::IBuffer* light_buffer() const { return m_LightBuffer; }
		nvrhi::IBuffer* cluster_counts() const { return m_ClusterCounts; }   // CLUSTER_COUNT uints, written by the light culling pass
		nvrhi::IBuffer* cluster_indices() const { return m_ClusterIndices; } // CLUSTER_COUNT * MAX_LIGHTS_PER_CLUSTER uints

	private:
		nvrhi::TextureHandle make_shadow_texture(u32 width, u32 height, u32 slices, const char* name);

		nvrhi::IDevice* m_Device = nullptr;
		bool m_Active = false;
		bool m_HasCamera = false;
		bool m_AoActive = false;
		ViewportTarget* m_Viewport = nullptr;
		GpuResourceCache* m_Cache = nullptr;
		const RenderScene* m_Scene = nullptr;
		SceneSettings m_Settings{};
		RenderQuality m_Quality{};
		FrameConstants m_Constants{};
		std::vector<DrawItem> m_Opaque{};
		std::vector<DrawItem> m_Masked{};
		std::vector<DrawItem> m_Blended{};
		u32 m_VisibleInstances = 0;
		bool visible(const Frustum& frustum, const glm::mat4& world, const glm::vec3& boundsMin, const glm::vec3& boundsMax) const;
		void collect_casters(const glm::mat4& viewProjection, u32 matrixIndex, std::vector<DrawItem>& items);
		std::vector<const GpuMesh*> m_Meshes{}; // per instance slot, looked up once per frame
		std::vector<GpuLight> m_Lights{};
		u32 m_DroppedLights = 0;
		bool m_Culling = true;
		bool m_ClusterCulling = true;
		std::chrono::steady_clock::time_point m_LastLightWarning{};

		std::vector<ShadowView> m_ShadowViews{};
		bool m_NeedCascades = false;
		bool m_NeedSpotAtlas = false;
		nvrhi::TextureHandle m_CascadeMap{};
		nvrhi::TextureHandle m_SpotAtlas{};
		nvrhi::TextureHandle m_DummyCascadeMap{};
		nvrhi::TextureHandle m_DummySpotAtlas{};
		nvrhi::SamplerHandle m_ShadowSampler{};
		nvrhi::SamplerHandle m_EnvironmentSampler{};
		nvrhi::TextureHandle m_DummyCube{};
		nvrhi::TextureHandle m_DummyBrdf{};
		nvrhi::TextureHandle m_DummyAo{};    // 1x1, white: no occlusion
		nvrhi::BindingLayoutHandle m_AoLayout{};
		nvrhi::BindingSetHandle m_AoBindings{};
		EnvironmentCache* m_Environments = nullptr;
		const GpuEnvironment* m_FrameEnvironment = nullptr; // what build() chose for this frame (null: the hemisphere)
		bool m_EnvironmentDummiesWritten = false;
		SkyKind m_SkyKind = SkyKind::None;
		const nvrhi::ITexture* m_BoundCube = nullptr;
		const nvrhi::ITexture* m_BoundBrdf = nullptr;
		const nvrhi::ITexture* m_BoundAo = nullptr;
		bool m_DummiesCleared = false;
		u32 m_ShadowGeneration = 0;
		const nvrhi::ITexture* m_BoundCascades = nullptr;
		const nvrhi::ITexture* m_BoundAtlas = nullptr;

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
