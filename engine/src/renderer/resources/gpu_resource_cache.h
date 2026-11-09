#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <assets/asset_manager.h>
#include <assets/particle_effect.h>

#include "../upload_queue.h"
#include "gpu_material.h"
#include "gpu_mesh.h"
#include "range_allocator.h"
#include "surface_shader_cache.h"

namespace wiwa
{
	// The GPU side of the assets the scene uses: meshes in shared vertex and index buffers, textures, and materials (a record in
	// one structured buffer plus a small binding set of the material's textures). Keyed by the resolved asset guid. Render thread only.
	//
	// The first ask for an asset starts loading it through the AssetManager (without waiting); when it is Ready, the cache uploads
	// it through the UploadQueue, a frame's byte budget at a time, and lets go of the CPU copy. Until a texture has arrived the
	// material uses fallback textures and switches by itself. A broken asset is logged once and stays unusable (a mesh is skipped,
	// a texture falls back, a material turns magenta). When the editor cooks an asset again, invalidate() loads the new version and
	// replaces the old one when it is ready.
	//
	// The vertex and index buffers come in pages (a few MiB each; a mesh larger than a page gets one of its own), so growing never
	// has to copy the GPU data; a GpuMesh says which page it lives in. Freed ranges are reused only after the frames that may still
	// be reading them have finished.
	//
	// A material with a surface shader (MaterialData::Shader) also has a custom record (GpuCustomRecord, same slot) that points at
	// its parameter block in one byte buffer of all blocks, and a binding set of its own (custom_layout(): its @param textures, the
	// custom records, the parameter bytes, the user sampler). While its shader is loading the material shows the default record,
	// once the shader failed the magenta one; either way get_material_shader() is 0 and the custom bit is clear.
	class GpuResourceCache
	{
	public:
		// api picks the binary target of the surface shaders (SPIR-V for Vulkan, DXBC otherwise).
		GpuResourceCache(nvrhi::IDevice* device, AssetManager* assets, UploadQueue* uploads, GraphicsApi api);
		~GpuResourceCache();

		GpuResourceCache(const GpuResourceCache&) = delete;
		GpuResourceCache& operator=(const GpuResourceCache&) = delete;

		// Null until the mesh is uploaded (and for good if it failed). Asking starts the load.
		const GpuMesh* get_mesh(const AssetRef& mesh);

		// The material's slot in the material buffer, at once: the record is the default one until the material is loaded.
		// An invalid reference is slot 0, the default grey material.
		u32 get_material_slot(const AssetRef& material);
		// The material's textures as one binding set (layout: material_layout()). Never null: fallback textures stand in.
		nvrhi::IBindingSet* get_material_bindings(u32 slot);
		// The record of a slot as the shaders will see it (the default one for a slot that does not exist).
		const GpuMaterialRecord& material_record(u32 slot) const { return slot < m_Records.size() ? m_Records[slot] : m_Records[0]; }
		nvrhi::IBindingLayout* material_layout() const { return m_MaterialLayout; }

		// The surface shader index (SurfaceShaderCache) the slot draws with: 0 for built-in materials and for materials whose
		// shader is loading or failed.
		u32 get_material_shader(u32 slot) const { return slot < m_SlotShaders.size() ? m_SlotShaders[slot] : 0; }
		// The custom record of a slot as the shaders will see it (zeros for a slot that draws built-in).
		const GpuCustomRecord& custom_record(u32 slot) const { return slot < m_CustomRecords.size() ? m_CustomRecords[slot] : m_CustomRecords[0]; }
		// The custom-material layout (set 3 on Vulkan): t16..t19 the @param textures, t20 custom records, t21 parameter bytes, s15.
		nvrhi::IBindingLayout* custom_layout() const { return m_CustomLayout; }
		// The slot's custom-material binding set. Never null: a built-in slot gets one with fallback textures.
		nvrhi::IBindingSet* get_custom_bindings(u32 slot);
		SurfaceShaderCache& surface_shaders() { return *m_SurfaceShaders; }
		// One GpuCustomRecord (16 bytes) per slot; replaced together with material_buffer().
		nvrhi::IBuffer* custom_record_buffer() const { return m_CustomBuffer; }
		// Every parameter block, at 16-byte aligned offsets. Replaced when it has to grow: material_generation() changes then.
		nvrhi::IBuffer* param_buffer() const { return m_ParamBuffer; }
		u64 param_buffer_size() const { return m_ParamRanges.capacity(); }

		// The uploaded texture, null until it is (and for good if it failed). Asking starts the load.
		nvrhi::ITexture* get_texture(const AssetRef& texture);

		// The signed distance field atlas of a font (R8), null until it is uploaded (and for good if it failed). Asking starts the load.
		nvrhi::ITexture* get_font_atlas(const AssetRef& font);

		u32 page_count() const { return static_cast<u32>(m_Pages.size()); }
		nvrhi::IBuffer* vertex_buffer(u32 page) const { return m_Pages[page].Vertex; }
		nvrhi::IBuffer* index_buffer(u32 page) const { return m_Pages[page].Index; }
		// The skin vertices (four joints and weights each) of the meshes on the page, parallel to its vertex buffer; null until a skinned mesh is placed on it.
		nvrhi::IBuffer* skin_buffer(u32 page) const { return m_Pages[page].Skin; }
		// All materials, 64 bytes each (GpuMaterialRecord). Replaced when it has to grow: material_generation() changes then.
		nvrhi::IBuffer* material_buffer() const { return m_MaterialBuffer; }
		u32 material_generation() const { return m_MaterialGeneration; }

		// The data of a particle effect (.wfx): CPU-side, the particle pass builds its own GPU records from it. Null while it loads or when it cannot be
		// loaded. effect_revision counts up each time the effect is cooked again (a system that holds ranges for it must start over).
		const ParticleEffectData* get_particle_effect(const AssetRef& effect);
		u32 effect_revision(const AssetRef& effect) const;

		// Once per frame, after UploadQueue::begin_frame and before its submit: polls what is loading and uploads what is ready.
		void update();

		// The asset with this resolved guid was cooked again (the editor's reload listener).
		void invalidate(AssetGuid resolved);

		// Assets that live in memory only, for tests and tools: the returned reference works like any other.
		AssetRef add_inline_mesh(MeshData mesh);
		AssetRef add_inline_material(MaterialData material);
		AssetRef add_inline_texture(TextureData texture);
		AssetRef add_inline_font(FontData font);
		AssetRef add_inline_particle_effect(ParticleEffectData effect);
		void set_inline_particle_effect(const AssetRef& effect, ParticleEffectData data);
		// Takes the inline data of an effect away again (a preview of unsaved edits): the effect is the asset on disk once more.
		void clear_inline_particle_effect(const AssetRef& effect);
		// Replaces the data of an inline asset; invalidate() the reference's guid for the change to show.
		void set_inline_texture(const AssetRef& texture, TextureData data);
		void set_inline_mesh(const AssetRef& mesh, MeshData data);
		void set_inline_material(const AssetRef& material, MaterialData data);

		struct Stats
		{
			u32 Meshes = 0;    // uploaded
			u32 Materials = 0; // slots in use, the default one included
			u32 Textures = 0;  // uploaded
			u64 Bytes = 0;     // GPU memory used by meshes (the ranges in use) and textures
			u32 Pending = 0;   // loading or waiting for upload room
			u32 Failed = 0;
			u32 Reloads = 0;   // assets replaced after a re-import
		};
		Stats get_stats() const;

	private:
		template<typename T>
		struct Source
		{
			Handle<T> Loaded{};
			Ref<const T> Inline{};

			LoadState state() const { return Inline ? LoadState::Ready : Loaded.state(); }
			const T* get() const { return Inline ? Inline.get() : Loaded.get(); }
			void reset()
			{
				Loaded = Handle<T>{};
				Inline.reset();
			}
		};

		struct Page
		{
			nvrhi::BufferHandle Vertex{};
			nvrhi::BufferHandle Index{};
			nvrhi::BufferHandle Skin{};
			u64 VertexCapacity = 0;
			RangeAllocator VertexRanges{};
			RangeAllocator IndexRanges{};
		};

		struct Placement
		{
			u32 Page = 0;
			u64 VertexOffset = 0;
			u64 VertexCount = 0;
			u64 IndexOffset = 0;
			u64 IndexCount = 0;
		};

		struct MeshEntry
		{
			AssetRef Ref{};
			Source<MeshData> Data{};
			GpuMesh Mesh{};
			Placement Where{};
			bool Uploaded = false;
			bool Failed = false;
			bool Reloading = false;
		};

		struct TextureEntry
		{
			AssetRef Ref{};
			Source<TextureData> Data{};
			nvrhi::TextureHandle Texture{};
			u64 Bytes = 0;
			bool Failed = false;
			bool Reloading = false;
		};

		struct FontEntry
		{
			AssetRef Ref{};
			Source<FontData> Data{};
			nvrhi::TextureHandle Atlas{};
			u64 Bytes = 0;
			bool Failed = false;
			bool Reloading = false;
		};

		struct MaterialEntry
		{
			AssetRef Ref{};
			Source<MaterialData> Data{};
			u32 Slot = 0;
			bool HasData = false;
			bool Failed = false;
			bool Reloading = false;
			AssetRef Textures[4]{}; // base color, normal, ORM, emissive
			nvrhi::ITexture* Bound[4]{}; // what the binding set was built with
			bool Settled = false;   // every texture is uploaded or failed

			// The record built from the data (what the slot shows without a shader, or with a ready one plus the custom bit).
			GpuMaterialRecord Builtin{};
			// The surface shader (MaterialData::Shader) and its parameters.
			u32 ShaderIndex = 0;
			std::vector<u8> ParamBytes{};
			AssetRef ParamTextures[SURFACE_MAX_TEXTURES]{};
			nvrhi::ITexture* ParamBound[SURFACE_MAX_TEXTURES]{}; // what the custom binding set was built with
			u64 ParamOffset = 0;   // the block's range in the parameter buffer, in bytes
			u64 ParamCapacity = 0; // 0: no block
			bool WarnedParamSize = false;
		};

		struct RetiredParams
		{
			u64 Offset = 0;
			u64 Size = 0;
			u64 Frame = 0;
		};

		struct RetiredRange
		{
			Placement Where{};
			u64 Frame = 0;
		};

		template<typename T>
		Source<T> acquire(const AssetRef& ref, const std::unordered_map<AssetGuid, Ref<const T>>& inlineAssets);

		TextureEntry& texture_entry(const AssetRef& ref);
		FontEntry& font_entry(const AssetRef& ref);
		void update_fonts();
		bool upload_font(FontEntry& entry, const FontData& data);
		void update_meshes();
		void update_textures();
		void update_materials();
		static bool has_skin(const MeshData& data);
		bool upload_mesh(MeshEntry& entry, const MeshData& data);
		bool upload_texture(TextureEntry& entry, const TextureData& data);
		std::optional<Placement> place(u64 vertexCount, u64 indexCount);
		void release_retired();
		void write_material_record(u32 slot);
		void ensure_material_capacity(u32 slots);
		void create_fallbacks();
		nvrhi::ITexture* texture_or_fallback(const AssetRef& ref, u32 index);
		nvrhi::BindingSetHandle make_bindings(nvrhi::ITexture* const textures[4]);
		void build_record(MaterialEntry& entry, const MaterialData& data);
		void compose_record(MaterialEntry& entry);
		void release_params(MaterialEntry& entry);
		bool place_params(MaterialEntry& entry, u64 bytes);
		void grow_param_buffer(u64 atLeast);
		void write_params(const MaterialEntry& entry);
		void update_custom_bindings(MaterialEntry& entry, bool force, bool& settled);
		void rebuild_custom_bindings();
		nvrhi::BindingSetHandle make_custom_bindings(nvrhi::ITexture* const textures[SURFACE_MAX_TEXTURES]);

		nvrhi::IDevice* m_Device;
		AssetManager* m_Assets;
		UploadQueue* m_Uploads;
		u64 m_Frame = 0;

		std::unordered_map<AssetGuid, Ref<const MeshData>> m_InlineMeshes{};
		std::unordered_map<AssetGuid, Ref<const MaterialData>> m_InlineMaterials{};
		std::unordered_map<AssetGuid, Ref<const TextureData>> m_InlineTextures{};

		std::unordered_map<AssetGuid, std::unique_ptr<MeshEntry>> m_Meshes{};
		std::unordered_map<AssetGuid, std::unique_ptr<TextureEntry>> m_Textures{};
		std::unordered_map<AssetGuid, Ref<const FontData>> m_InlineFonts{};
		std::unordered_map<AssetGuid, Ref<const ParticleEffectData>> m_InlineEffects{};
		struct EffectEntry
		{
			Source<ParticleEffectData> Data{};
			u32 Revision = 0;
		};
		std::unordered_map<AssetGuid, EffectEntry> m_Effects{};
		std::unordered_map<AssetGuid, std::unique_ptr<FontEntry>> m_Fonts{};
		std::vector<FontEntry*> m_PendingFonts{};
		std::unordered_map<AssetGuid, std::unique_ptr<MaterialEntry>> m_MaterialEntries{};
		std::vector<MeshEntry*> m_PendingMeshes{};
		std::vector<TextureEntry*> m_PendingTextures{};
		std::vector<MaterialEntry*> m_PendingMaterials{};
		std::vector<Page> m_Pages{};
		std::vector<RetiredRange> m_Retired{};

		// Material slot -> record, binding set. Slot 0 is the default material.
		std::vector<GpuMaterialRecord> m_Records{};
		std::vector<nvrhi::BindingSetHandle> m_Bindings{};
		std::vector<u32> m_DirtyRecords{};
		nvrhi::BufferHandle m_MaterialBuffer{};
		u32 m_MaterialCapacity = 0;
		u32 m_MaterialGeneration = 0;

		// Custom materials, per slot: the shader index it draws with (0: built-in), the custom record, the custom binding set.
		Scope<SurfaceShaderCache> m_SurfaceShaders{};
		u32 m_SurfaceGeneration = 0; // the shader cache's generation the records were composed with
		std::vector<u32> m_SlotShaders{};
		std::vector<GpuCustomRecord> m_CustomRecords{};
		std::vector<nvrhi::BindingSetHandle> m_CustomBindings{};
		nvrhi::BufferHandle m_CustomBuffer{};
		nvrhi::BufferHandle m_ParamBuffer{};
		RangeAllocator m_ParamRanges{};
		std::vector<RetiredParams> m_RetiredParams{};
		nvrhi::BindingLayoutHandle m_CustomLayout{};
		nvrhi::SamplerHandle m_CustomSampler{};
		nvrhi::BindingSetHandle m_FallbackCustomBindings{};

		nvrhi::BindingLayoutHandle m_MaterialLayout{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::TextureHandle m_FallbackTextures[4]{}; // white, flat normal, neutral ORM, black
		nvrhi::BindingSetHandle m_FallbackBindings{};
		bool m_FallbacksUploaded = false;

		u32 m_Reloads = 0;
		u32 m_FailedCount = 0;
	};
}
