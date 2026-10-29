#pragma once

// The assets the engine itself relies on, named by the path of their source in the engine/ source root. The asset catalog
// (see assets/asset_catalog.h) turns a path into the asset; no guid is written in code.
namespace wiwa::engine_assets
{
	inline constexpr const char* IMGUI_SHADER = "engine/shaders/imgui.hlsl";
	inline constexpr const char* DEPTH_SHADER = "engine/shaders/depth.hlsl";
	inline constexpr const char* DEPTH_MASK_SHADER = "engine/shaders/depth_mask.hlsl";
	inline constexpr const char* LIGHT_CULL_SHADER = "engine/shaders/light_cull.hlsl";
	inline constexpr const char* SHADOW_SHADER = "engine/shaders/shadow.hlsl";
	inline constexpr const char* FORWARD_SHADER = "engine/shaders/forward.hlsl";
	inline constexpr const char* BLIT_SHADER = "engine/shaders/blit.hlsl";
	inline constexpr const char* SKY_SHADER = "engine/shaders/sky.hlsl";
	inline constexpr const char* SKY_ATMOSPHERE_SHADER = "engine/shaders/sky_atmosphere.hlsl";
	inline constexpr const char* TONEMAP_SHADER = "engine/shaders/tonemap.hlsl";
}
