#pragma once

#include <assets/asset_guid.h>

// The assets the engine itself relies on, by the guids of their .meta files (engine/ source root). Those .meta files are
// committed and must not be regenerated or deleted: the guid in them is the one named here.
namespace wiwa::engine_assets
{
	// engine/shaders/imgui.hlsl
	inline AssetRef imgui_shader()
	{
		return AssetRef{ AssetGuid{ 0x41caf8c4396f32d8ull, 0xafc8d91acc825038ull }, 0 };
	}
}
