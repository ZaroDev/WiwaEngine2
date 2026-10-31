#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <assets/asset_manager.h>

#include "../backend/render_backend.h"

namespace wiwa
{
	// The NVRHI shaders of one surface shader (.wshader): a vertex and a pixel shader per pass variant, for the backend in use.
	struct GpuSurfaceShader
	{
		nvrhi::ShaderHandle Shaders[SURFACE_VARIANT_COUNT][2]{}; // [SurfaceVariantKind][0 vertex, 1 pixel]
		u32 TextureCount = 0;
		u32 BlockSize = 0;      // bytes of the parameter block the shader reads
		bool Failed = false;    // no usable version and none coming
		bool Loading = true;    // no version yet, one is on its way
		u32 Generation = 0;     // goes up each time a version is put in use

		bool ready() const { return !Failed && !Loading; }
		nvrhi::IShader* get(SurfaceVariantKind kind, ShaderStage stage) const
		{
			return Shaders[static_cast<u32>(kind)][stage == ShaderStage::Vertex ? 0 : 1];
		}
	};

	// Turns cooked SurfaceShaderData into NVRHI shaders, as ShaderLibrary does for engine shaders. Render thread only.
	//
	// A shader gets an index on the first ask (0 means none, the built-in material), which stays the same for as long as the
	// cache lives. A reload keeps the version in use until the new one is ready; a broken reload keeps it for good. A shader
	// that never had a usable version (load failed, or a variant missing for this backend) is Failed.
	class SurfaceShaderCache
	{
	public:
		SurfaceShaderCache(nvrhi::IDevice* device, GraphicsApi api, AssetManager* assets);
		~SurfaceShaderCache();

		SurfaceShaderCache(const SurfaceShaderCache&) = delete;
		SurfaceShaderCache& operator=(const SurfaceShaderCache&) = delete;

		// 0 for an invalid reference; asking starts the load.
		u32 get_shader_index(const AssetRef& shader);
		// Null for index 0 and for an index that was never given out.
		const GpuSurfaceShader* get(u32 index) const;
		// Changes whenever a shader appears, is replaced (reload) or fails: when pipelines and records built from it are stale.
		u32 generation() const { return m_Generation; }

		// Polls the loads and creates the shader objects of what is ready. Once per frame.
		void update();

		// The asset was cooked again: loads the new version (the old one stays in use meanwhile).
		void invalidate(AssetGuid resolved);

		// Surface shaders that live in memory only, for tests: the returned reference works like any other.
		AssetRef add_inline_shader(SurfaceShaderData shader);
		// Replaces the data of an inline shader; invalidate() the reference's guid for the change to show.
		void set_inline_shader(const AssetRef& shader, SurfaceShaderData data);

		// Whether any shader is still waiting for its first version.
		bool any_loading() const;

	private:
		struct Entry
		{
			AssetRef Asset{};
			Handle<SurfaceShaderData> Pending{};
			Ref<const SurfaceShaderData> PendingInline{};
			bool HasPending = false;
			GpuSurfaceShader Gpu{};
		};

		void start_load(Entry& entry);
		void finish(Entry& entry, const SurfaceShaderData* data);

		nvrhi::IDevice* m_Device;
		GraphicsApi m_Api;
		AssetManager* m_Assets;

		std::unordered_map<AssetGuid, Ref<const SurfaceShaderData>> m_Inline{};
		std::unordered_map<AssetGuid, u32> m_Indices{};
		std::vector<std::unique_ptr<Entry>> m_Entries{}; // index - 1
		u32 m_Generation = 0;
	};
}
