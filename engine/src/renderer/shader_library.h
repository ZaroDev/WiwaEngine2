#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <assets/asset_manager.h>

#include "backend/render_backend.h"

namespace wiwa
{
	// The cooked binary a backend loads: SPIR-V for Vulkan, DXBC for D3D11 and D3D12.
	inline ShaderTarget shader_target_for(GraphicsApi api)
	{
		return api == GraphicsApi::Vulkan ? ShaderTarget::Spirv : ShaderTarget::Dxbc;
	}

	// Turns the cooked Shader assets into NVRHI shaders for the backend in use. Render thread only. Nothing is compiled
	// here: the binary of the right target (DXBC for D3D11 and D3D12, SPIR-V for Vulkan) is taken from the asset.
	//
	// Shaders are named by the path of their source ("engine/shaders/imgui.hlsl"). The path is resolved through the asset
	// catalog, which the editor may still be writing when the renderer starts, so a path that is not known yet is tried
	// again every frame until it is.
	//
	// A pass asks for the stages it needs in initialize() and polls get() in update(); get_generation() goes up whenever a
	// shader object appears or is replaced (hot reload), which is when a pass has to rebuild the pipelines that use it.
	class ShaderLibrary
	{
	public:
		ShaderLibrary(nvrhi::IDevice* device, GraphicsApi api, AssetManager* assets);

		// Starts loading one stage of a shader asset. Asking again for the same one does nothing.
		void request(const std::string& path, ShaderStage stage);

		// Null until the shader is ready (and for a shader that failed).
		nvrhi::IShader* get(const std::string& path, ShaderStage stage) const;
		// True when this shader has no object and none is coming (a version in use that a reload could not replace is not a failure).
		bool has_failed(const std::string& path, ShaderStage stage) const;

		// Resolves paths that are known now and creates the shader objects of assets that finished loading. Once per frame.
		void update();

		// The asset was cooked again: loads the new version. The old shader object stays in use until the new one is ready,
		// and if the new one is broken it stays for good.
		void invalidate(AssetGuid resolvedGuid);

		// Every requested shader has an object / at least one has none and cannot get one.
		bool all_ready() const;
		bool any_failed() const;
		u32 get_generation() const { return m_Generation; }

		// "ready", "loading" or "failed", for the stats panel.
		const char* get_status() const;

	private:
		struct Entry
		{
			std::string Path{};
			ShaderStage Stage = ShaderStage::Vertex;
			AssetRef Ref{};               // valid once the path is resolved
			Handle<ShaderData> Pending{}; // the version being loaded
			nvrhi::ShaderHandle Shader{}; // the version in use
			bool Failed = false;          // no usable shader and none coming
			bool WarnedUnknown = false;
			std::chrono::steady_clock::time_point Requested{};
		};

		Entry* find(const std::string& path, ShaderStage stage);
		const Entry* find(const std::string& path, ShaderStage stage) const;
		void finish(Entry& entry);

		nvrhi::IDevice* m_Device;
		GraphicsApi m_Api;
		AssetManager* m_Assets;
		std::vector<Entry> m_Entries{};
		u32 m_Generation = 0;
	};
}
