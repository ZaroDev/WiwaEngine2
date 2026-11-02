#pragma once

// The assets the engine itself relies on, named by the path of their source in the engine/ source root. The asset catalog
// (see assets/asset_catalog.h) turns a path into the asset; no guid is written in code.
namespace wiwa::engine_assets
{
	inline constexpr const char* IMGUI_SHADER = "engine/shaders/imgui.hlsl";
	inline constexpr const char* UI_SHADER = "engine/shaders/ui.hlsl";
	inline constexpr const char* DEPTH_SHADER = "engine/shaders/depth.hlsl";
	inline constexpr const char* DEPTH_MASK_SHADER = "engine/shaders/depth_mask.hlsl";
	inline constexpr const char* LIGHT_CULL_SHADER = "engine/shaders/light_cull.hlsl";
	inline constexpr const char* SHADOW_SHADER = "engine/shaders/shadow.hlsl";
	inline constexpr const char* FORWARD_SHADER = "engine/shaders/forward.hlsl";
	inline constexpr const char* VELOCITY_BLEND_SHADER = "engine/shaders/velocity_blend.hlsl";
	inline constexpr const char* BLIT_SHADER = "engine/shaders/blit.hlsl";
	inline constexpr const char* SKY_SHADER = "engine/shaders/sky.hlsl";
	inline constexpr const char* SKY_ATMOSPHERE_SHADER = "engine/shaders/sky_atmosphere.hlsl";
	inline constexpr const char* EXPOSURE_HISTOGRAM_SHADER = "engine/shaders/exposure_histogram.hlsl";
	inline constexpr const char* EXPOSURE_RESOLVE_SHADER = "engine/shaders/exposure_resolve.hlsl";
	inline constexpr const char* BLOOM_DOWN_SHADER = "engine/shaders/bloom_down.hlsl";
	inline constexpr const char* BLOOM_UP_SHADER = "engine/shaders/bloom_up.hlsl";
	inline constexpr const char* FXAA_SHADER = "engine/shaders/fxaa.hlsl";
	inline constexpr const char* TAA_SHADER = "engine/shaders/taa.hlsl";
	inline constexpr const char* MOTION_BLUR_SHADER = "engine/shaders/motion_blur.hlsl";
	inline constexpr const char* GTAO_DEPTH_SHADER = "engine/shaders/gtao_depth.hlsl";
	inline constexpr const char* GTAO_SHADER = "engine/shaders/gtao.hlsl";
	inline constexpr const char* GTAO_BLUR_SHADER = "engine/shaders/gtao_blur.hlsl";
	inline constexpr const char* TONEMAP_SHADER = "engine/shaders/tonemap.hlsl";
	inline constexpr const char* SELECTION_OUTLINE_SHADER = "engine/shaders/selection_outline.hlsl";
	inline constexpr const char* PICK_SHADER = "engine/shaders/pick.hlsl";
	inline constexpr const char* PICK_ICON_SHADER = "engine/shaders/pick_icon.hlsl";
	inline constexpr const char* DEBUG_LINE_SHADER = "engine/shaders/debug_line.hlsl";
	inline constexpr const char* DEBUG_TRIANGLE_SHADER = "engine/shaders/debug_triangle.hlsl";
	// Only the render checks use it: draws 1 where the stencil of a depth texture is 1, to prove the stencil can be sampled.
	inline constexpr const char* STENCIL_PROBE_SHADER = "engine/shaders/stencil_probe.hlsl";
}
