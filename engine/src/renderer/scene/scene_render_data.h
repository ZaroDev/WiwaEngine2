#pragma once

#include <chrono>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "../resources/gpu_resource_cache.h"
#include "../environment/environment_cache.h"
#include "../render_constants.h"
#include "../render_quality.h"
#include "../view/view_types.h"
#include "../viewport_target.h"
#include "render_scene.h"

namespace wiwa
{
	class Frustum;
	// The cluster grid, the shadow map sizes and NO_PALETTE are in render_constants.h, which the shaders read too.
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
		u32 Material = 0;   // slot in the material buffer (a shadow caster carries its shadow matrix index here)
		u32 Slot = 0;       // the material's own slot, also for a shadow caster
		u32 Shader = 0;     // surface shader index (SurfaceShaderCache) the material draws with; 0 is the built-in material
		bool Masked = false; // alpha-masked (picks the masked variant of a surface shader)
		u32 Page = 0;       // which shared vertex / index buffer pair
		u32 FirstIndex = 0;
		u32 IndexCount = 0;
		i32 BaseVertex = 0;
		bool DoubleSided = false;
		bool ReceivesDecals = true; // false: the depth prepass marks its pixels in the stencil and the decals leave them alone
		float Depth = 0.0f; // view-space distance of the primitive's bounds center (for sorting blended draws)
	};

	// One scene instance as the shaders read it (SceneInstance in scene_common.hlsli): the world matrix, the matrix for normals and the
	// world matrix of the previous frame, each as four columns.
	struct GpuInstance
	{
		glm::mat4 World{ 1.0f };
		glm::mat4 Normal{ 1.0f };
		glm::mat4 PrevWorld{ 1.0f };
		glm::uvec4 Skin{ NO_PALETTE, NO_PALETTE, 0, 0 }; // x, y: row of this frame's and the previous frame's joint palette in uPalettes; NO_PALETTE: a rigid instance
	};
	static_assert(sizeof(GpuInstance) == 208);

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
		glm::mat4 ShadowMatrices[MAX_CASCADES + MAX_SPOT_SHADOWS]{};    // 0..3 the sun's cascades, 4..7 the shadowed spot lights
		glm::vec4 CascadeSplits{ 0.0f };  // view depth at which each cascade ends
		glm::vec4 CascadeTexels{ 0.0f };  // world size of a texel of each cascade
		glm::vec4 ShadowParams{ 0.0f };   // x = 1 when the sun casts shadows, y = cascade count, z = depth bias, w = normal bias (texels)
		glm::vec4 ShadowParams2{ 0.0f };  // x = spot depth bias
		glm::vec4 SpotShadowRects[MAX_SPOT_SHADOWS]{};   // atlas tile of each shadowed spot light: offset xy, scale zw
		glm::vec4 SpotShadowInfo[MAX_SPOT_SHADOWS]{};    // x = tan(half the cone angle)
		glm::vec4 EnvSH[9]{};             // the environment's diffuse light (rgb in xyz)
		glm::vec4 EnvParams{ 0.0f };      // x = 1 when an environment lights the scene, y = highest mip, z = brightness * ambient intensity, w = 1 when the BRDF table exists
		glm::vec4 EnvRotation{ 1.0f, 0.0f, 1.0f, 0.0f }; // cos, sin of the rotation about +Y, the brightness of the sky seen
		glm::mat4 InverseViewProjection{ 1.0f };
		glm::vec4 AtmosphereA{ 1.0f, 1.0f, 1.0f, 0.8f }; // Rayleigh scale, Mie scale, ozone scale, Mie anisotropy
		glm::vec4 AtmosphereB{ 0.6f, 0.05f, 24.0f, 8.0f }; // multiple scattering, observer altitude (km), steps along the view ray, steps towards the sun
		glm::vec4 AtmosphereGround{ 0.05f, 0.08f, 0.1f, 120.0f }; // ground albedo, sun disc boost
		glm::vec4 AtmosphereSun{ 0.0f, 1.0f, 0.0f, 0.0f };        // towards the sun
		glm::vec4 AtmosphereIrradiance{ 3.4f, 3.4f, 3.4f, 3.5f }; // the sun outside the atmosphere, sky brightness
		glm::mat4 UnjitteredViewProjection{ 1.0f };  // what picking, debug shapes, culling and the velocity use; Projection and ViewProjection carry the TAA jitter
		glm::mat4 PreviousViewProjection{ 1.0f };    // the previous frame's unjittered one (this frame's when there is no history)
		glm::vec4 JitterInfo{ 0.0f };                // xy = the jitter in pixels, z = 1 when TAA jitters the projection, w = 1 when the previous camera is a history
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
		// shareLayoutsFrom: the data of another view. Every view uses the binding layouts of the first one, so that a pipeline made with them
		// fits the binding sets of all views (pipelines are made once, not per view).
		bool initialize(nvrhi::IDevice* device, const SceneRenderData* shareLayoutsFrom = nullptr);
		void shutdown();

		// Culls the scene against the effective camera and fills the draw lists and the constants. Without a viewport target there
		// is nothing to draw into and the data is inactive.
		void build(RenderScene& scene, GpuResourceCache& cache, ViewportTarget& viewport);

		// What the view this data belongs to switches on (the main view is Full). Applied by build(): the settings copy loses the effects the
		// view does not run, shadows are cut down, light culling can go.
		void set_view_flags(const ViewFlags& flags) { m_Flags = flags; }
		// The background is nothing (alpha 0), not the sky or the colour: views made for UI widgets and thumbnails. The alpha of the HDR image is
		// coverage then, and the LDR image holds straight alpha. Never for the main view.
		void set_transparent_background(bool transparent) { m_TransparentBackground = transparent; }
		bool transparent_background() const { return m_TransparentBackground; }

		// The index of the frame about to be built: it picks the jitter.
		void set_frame_index(u64 index) { m_FrameIndex = index; }
		// TAA jitters the projection of this frame (it is on and there is a camera).
		bool temporal_active() const { return m_TemporalActive; }
		// The camera of the previous frame is a usable history: not the first frame, no resize, scene reset, camera switch or cut in
		// between, and nobody asked for a reset. Passes that reproject (TAA, motion blur) use the current frame alone otherwise.
		bool history_valid() const { return m_HistoryValid; }
		// Counts every frame in which the history was not valid.
		u32 history_reset_count() const { return m_HistoryResetCount; }
		// The jitter of this frame in pixels (zero without TAA).
		glm::vec2 jitter() const { return glm::vec2(m_Constants.JitterInfo.x, m_Constants.JitterInfo.y); }
		// Shows the velocity of every pixel instead of the scene (the motion blur pass draws it); for looking at what TAA and motion blur see.
		void set_debug_velocity(bool enabled) { m_DebugVelocity = enabled; }
		bool debug_velocity() const { return m_DebugVelocity; }
		// The next frame starts without a history.
		void request_history_reset() { m_HistoryResetRequested = true; }
		// A camera that moves farther than this between two frames is a cut: there is no history across it.
		static constexpr float CAMERA_CUT_DISTANCE = 50.0f;
		// Writes the instance buffer (when the scene changed) and the constants; records into the frame's command list.
		void write(nvrhi::ICommandList* commandList, GpuResourceCache& cache);

		// There is a viewport target to draw into (the passes draw the background even when there is nothing else).
		bool active() const { return m_Active; }
		bool has_camera() const { return m_HasCamera; }
		// The scene has ambient occlusion on this frame: the AO pass writes ViewportTarget::ao() and the forward pass reads it (else a
		// white stand-in).
		bool ao_active() const { return m_AoActive; }
		// The forward pass reads the occlusion through a layout of its own (one SRV, t24): the frame layout's registers already span
		// 1..9, and NVRHI rejects layouts whose register ranges overlap those of the material layout (t10..t13).
		// The set also carries the four decal targets (t25..t28, stand-ins that change nothing when the view has no decals).
		nvrhi::IBindingLayout* ao_layout() const { return m_AoLayout; }
		nvrhi::IBindingSet* ao_bindings() const { return m_AoBindings; }

		// The decals this view draws, culled against the camera and sorted by SortOrder then slot (at most MAX_VISIBLE_DECALS; the rest are
		// counted in dropped_decals()). Slot indexes RenderScene::decals(); Material is the material slot of the decal's material.
		struct VisibleDecal
		{
			u32 Slot = 0;
			u32 Material = 0;
		};
		static constexpr u32 MAX_VISIBLE_DECALS = 512;
		const std::vector<VisibleDecal>& visible_decals() const { return m_VisibleDecals; }
		u32 dropped_decals() const { return m_DroppedDecals; }
		// The decal targets of the viewport exist and are the ones bound in ao_bindings() (decals are visible this frame).
		bool decals_active() const { return m_DecalsActive; }
		// An environment (a file's, ready) lights the scene this frame.
		// What the sky pass draws behind the scene this frame.
		enum class SkyKind { None, Environment, Atmosphere };
		SkyKind sky_kind() const { return m_SkyKind; }
		ViewportTarget* viewport() const { return m_Viewport; }
		GpuResourceCache* cache() const { return m_Cache; }
		const RenderScene* scene() const { return m_Scene; }
		// The scene of this view, to take what the particle simulation consumes (RenderScene::take_pending_spawns). Render thread, simulation pass only.
		RenderScene* scene_mutable() const { return m_Scene; }
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
		// The frame set with stand-ins for the shadow maps, for drawing into them (a surface shader's casters read the whole frame set).
		nvrhi::IBindingSet* shadow_frame_bindings() const { return m_ShadowFrameBindings; }
		// The skin set (joint palettes t22, the skin vertices of the page t23): the last set of every pass that draws meshes, so its layout is
		// the last of each pass layouts. A page without skinned meshes gets stand-ins. The frame layout cannot hold it: its registers span 1..9
		// and the material layout sits in t10..t13 (see ao_layout()).
		nvrhi::IBindingLayout* skin_layout() const { return m_SkinLayout; }
		nvrhi::IBindingSet* skin_bindings(u32 page) const;

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
		RenderScene* m_Scene = nullptr;
		SceneSettings m_Settings{};
		RenderQuality m_Quality{};
		ViewFlags m_Flags{};
		bool m_TransparentBackground = false;
		FrameConstants m_Constants{};
		u64 m_FrameIndex = 0;
		bool m_TemporalActive = false;
		bool m_HistoryValid = false;
		bool m_DebugVelocity = false;
		bool m_HistoryResetRequested = false;
		u32 m_HistoryResetCount = 0;
		// What the previous frame looked through, for the history checks and the previous matrix.
		bool m_PreviousKnown = false;
		glm::mat4 m_PreviousViewProjection{ 1.0f };
		glm::vec3 m_PreviousCameraPosition{ 0.0f };
		u32 m_PreviousViewportGeneration = 0;
		u32 m_PreviousSceneResets = 0;
		bool m_PreviousOverride = false;
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
		// 1x1, zero: no decals (albedo, normal, ORM, emissive). Read with the coordinates clamped, so one texel serves every pixel.
		nvrhi::TextureHandle m_DummyDecal[4]{};
		bool m_DummyDecalWritten = false;
		std::vector<VisibleDecal> m_VisibleDecals{};
		u32 m_DroppedDecals = 0;
		bool m_DecalsActive = false;
		u32 m_FramesWithoutDecals = 0;
		const nvrhi::ITexture* m_BoundDecalAlbedo = nullptr;
		u32 m_BoundDecalGeneration = 0;
		static constexpr u32 DECAL_TARGET_LINGER_FRAMES = 120;
		std::chrono::steady_clock::time_point m_LastDecalWarning{};
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
		nvrhi::BindingSetHandle m_ShadowFrameBindings{};
		nvrhi::BufferHandle m_ConstantBuffer{};
		nvrhi::BufferHandle m_InstanceBuffer{};
		u32 m_InstanceCapacity = 0;
		u64 m_WrittenRevision = ~0ull;
		const nvrhi::IBuffer* m_BoundInstanceBuffer = nullptr;
		const nvrhi::IBuffer* m_BoundMaterialBuffer = nullptr;
		std::vector<GpuInstance> m_InstanceScratch{};

		struct SkinSet
		{
			nvrhi::BindingSetHandle Set{};
			const nvrhi::IBuffer* Palette = nullptr;
			const nvrhi::IBuffer* Skin = nullptr;
		};
		void update_skin_sets(GpuResourceCache& cache);
		// Fills Skin of the instance scratch and the palette rows of the skinned instances; true when there are any.
		bool pack_palettes();
		nvrhi::BindingLayoutHandle m_SkinLayout{};
		nvrhi::BufferHandle m_DummyPalette{};
		nvrhi::BufferHandle m_DummySkin{};
		nvrhi::BufferHandle m_PaletteBuffer{};
		bool m_HadSkinned = false;                 // the last instance write had palettes: the next one clears them
		u32 m_PaletteCapacity = 0;                 // rows
		std::vector<glm::vec4> m_PaletteScratch{};
		SkinSet m_DefaultSkinSet{};
		std::vector<SkinSet> m_SkinSets{}; // per mesh page
	};
}
