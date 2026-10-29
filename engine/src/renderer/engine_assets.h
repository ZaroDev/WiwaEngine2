#pragma once

// The assets the engine itself relies on, named by the path of their source in the engine/ source root. The asset catalog
// (see assets/asset_catalog.h) turns a path into the asset; no guid is written in code.
namespace wiwa::engine_assets
{
	inline constexpr const char* IMGUI_SHADER = "engine/shaders/imgui.hlsl";
}
