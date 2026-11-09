#include <pch.h>
#include "gpu_resource_cache.h"

#include <algorithm>
#include <bit>

#include <assets/builtin_assets.h>

#define WI_CACHE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u64 PAGE_VERTICES = 256 * 1024;
		constexpr u64 PAGE_INDICES = 768 * 1024;
		constexpr u32 INITIAL_MATERIAL_CAPACITY = 256;
		constexpr u64 INITIAL_PARAM_BYTES = 4096; // 16 blocks of the largest size
		const glm::vec4 MAGENTA{ 1.0f, 0.0f, 1.0f, 1.0f };
		// A freed range may still be read by the frames in flight: it is reused after this many frames.
		constexpr u64 RETIRE_FRAMES = 6;

		static_assert(sizeof(MeshVertex) == 32, "the vertex buffer holds MeshVertex as is");

		nvrhi::VulkanBindingOffsets make_zero_binding_offsets()
		{
			nvrhi::VulkanBindingOffsets offsets;
			offsets.shaderResource = 0;
			offsets.sampler = 0;
			offsets.constantBuffer = 0;
			offsets.unorderedAccess = 0;
			return offsets;
		}

		nvrhi::Format texture_format(TextureKind kind)
		{
			return kind == TextureKind::Color ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		}

		bool valid_texture(const TextureData& data)
		{
			if (data.Width == 0 || data.Height == 0 || data.Mips.empty())
			{
				return false;
			}
			for (size_t mip = 0; mip < data.Mips.size(); mip++)
			{
				const size_t width = std::max(data.Width >> mip, 1u);
				const size_t height = std::max(data.Height >> mip, 1u);
				if (data.Mips[mip].size() != width * height * 4)
				{
					return false;
				}
			}
			return true;
		}

		u64 texture_bytes(const TextureData& data)
		{
			u64 bytes = 0;
			for (const std::vector<u8>& mip : data.Mips)
			{
				bytes += mip.size();
			}
			return bytes;
		}

		bool valid_mesh(const MeshData& data)
		{
			if (data.Vertices.empty() || data.Indices.empty() || data.Primitives.empty())
			{
				return false;
			}
			for (const MeshPrimitive& primitive : data.Primitives)
			{
				if (static_cast<u64>(primitive.FirstIndex) + primitive.IndexCount > data.Indices.size())
				{
					return false;
				}
			}
			return true;
		}

		template<typename T>
		void remove_one(std::vector<T*>& list, T* item)
		{
			list.erase(std::remove(list.begin(), list.end(), item), list.end());
		}

		template<typename T>
		void push_unique(std::vector<T*>& list, T* item)
		{
			if (std::find(list.begin(), list.end(), item) == list.end())
			{
				list.push_back(item);
			}
		}

		GpuMaterialRecord magenta_record()
		{
			GpuMaterialRecord magenta;
			magenta.BaseColor = MAGENTA;
			magenta.Roughness = 1.0f;
			return magenta;
		}

		u64 align_param_size(u64 bytes)
		{
			return (bytes + PARAM_BLOCK_ALIGNMENT - 1) / PARAM_BLOCK_ALIGNMENT * PARAM_BLOCK_ALIGNMENT;
		}
	}

	GpuResourceCache::GpuResourceCache(nvrhi::IDevice* device, AssetManager* assets, UploadQueue* uploads, GraphicsApi api)
		: m_Device(device)
		, m_Assets(assets)
		, m_Uploads(uploads)
	{
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Pixel;
		layout.bindingOffsets = make_zero_binding_offsets();
		for (u32 binding : { MATERIAL_BINDING_BASE_COLOR, MATERIAL_BINDING_NORMAL, MATERIAL_BINDING_ORM, MATERIAL_BINDING_EMISSIVE })
		{
			layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(binding));
		}
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(MATERIAL_BINDING_SAMPLER));
		m_MaterialLayout = m_Device->createBindingLayout(layout);

		const nvrhi::SamplerDesc samplerDescription = nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setMaxAnisotropy(8.0f)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
		m_Sampler = m_Device->createSampler(samplerDescription);

		create_fallbacks();

		// the meshes the engine makes itself (assets/builtin_assets.h): named by the main thread, held here
		m_InlineMeshes[builtin_assets::PREVIEW_SPHERE.resolved()] = std::make_shared<const MeshData>(builtin_assets::make_preview_sphere());

		// The custom-material layout: set 3 on Vulkan (its index among a pipeline's layouts), registers of its own on D3D.
		nvrhi::BindingLayoutDesc custom;
		custom.visibility = nvrhi::ShaderType::All; // the parameters are read by VertexOffset too
		custom.bindingOffsets = make_zero_binding_offsets();
		for (u32 t = 0; t < SURFACE_MAX_TEXTURES; t++)
		{
			custom.addItem(nvrhi::BindingLayoutItem::Texture_SRV(CUSTOM_BINDING_FIRST_TEXTURE + t));
		}
		custom.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(CUSTOM_BINDING_RECORDS));
		custom.addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(CUSTOM_BINDING_PARAMS));
		custom.addItem(nvrhi::BindingLayoutItem::Sampler(CUSTOM_BINDING_SAMPLER));
		m_CustomLayout = m_Device->createBindingLayout(custom);

		m_SurfaceShaders = CreateScope<SurfaceShaderCache>(m_Device, api, m_Assets);

		// Slot 0: the default material, for primitives that name none.
		m_Records.push_back(GpuMaterialRecord{});
		m_Bindings.push_back(m_FallbackBindings);
		m_SlotShaders.push_back(0);
		m_CustomRecords.push_back(GpuCustomRecord{});
		m_CustomBindings.push_back(nullptr);
		ensure_material_capacity(1);
		grow_param_buffer(INITIAL_PARAM_BYTES); // makes the custom binding sets
		m_DirtyRecords.push_back(0);
	}

	GpuResourceCache::~GpuResourceCache() = default;

	void GpuResourceCache::create_fallbacks()
	{
		for (nvrhi::TextureHandle& texture : m_FallbackTextures)
		{
			nvrhi::TextureDesc description;
			description.width = 1;
			description.height = 1;
			description.format = nvrhi::Format::RGBA8_UNORM;
			description.isShaderResource = true;
			description.initialState = UploadQueue::resting_state(m_Device);
			description.keepInitialState = true;
			description.debugName = "Material fallback texture";
			texture = m_Device->createTexture(description);
		}

		nvrhi::ITexture* textures[4] = { m_FallbackTextures[0], m_FallbackTextures[1], m_FallbackTextures[2], m_FallbackTextures[3] };
		m_FallbackBindings = make_bindings(textures);
	}

	nvrhi::BindingSetHandle GpuResourceCache::make_bindings(nvrhi::ITexture* const textures[4])
	{
		nvrhi::BindingSetDesc set;
		const u32 bindings[4] = { MATERIAL_BINDING_BASE_COLOR, MATERIAL_BINDING_NORMAL, MATERIAL_BINDING_ORM, MATERIAL_BINDING_EMISSIVE };
		for (u32 i = 0; i < 4; i++)
		{
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(bindings[i], textures[i]));
		}
		set.addItem(nvrhi::BindingSetItem::Sampler(MATERIAL_BINDING_SAMPLER, m_Sampler));
		return m_Device->createBindingSet(set, m_MaterialLayout);
	}

	nvrhi::BindingSetHandle GpuResourceCache::make_custom_bindings(nvrhi::ITexture* const textures[SURFACE_MAX_TEXTURES])
	{
		nvrhi::BindingSetDesc set;
		for (u32 t = 0; t < SURFACE_MAX_TEXTURES; t++)
		{
			nvrhi::ITexture* texture = textures[t] != nullptr ? textures[t] : m_FallbackTextures[0].Get(); // white
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(CUSTOM_BINDING_FIRST_TEXTURE + t, texture));
		}
		set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(CUSTOM_BINDING_RECORDS, m_CustomBuffer));
		set.addItem(nvrhi::BindingSetItem::RawBuffer_SRV(CUSTOM_BINDING_PARAMS, m_ParamBuffer));
		set.addItem(nvrhi::BindingSetItem::Sampler(CUSTOM_BINDING_SAMPLER, m_Sampler));
		return m_Device->createBindingSet(set, m_CustomLayout);
	}

	template<typename T>
	GpuResourceCache::Source<T> GpuResourceCache::acquire(const AssetRef& ref, const std::unordered_map<AssetGuid, Ref<const T>>& inlineAssets)
	{
		Source<T> source;
		const auto found = inlineAssets.find(ref.resolved());
		if (found != inlineAssets.end())
		{
			source.Inline = found->second;
		}
		else if (m_Assets != nullptr)
		{
			source.Loaded = m_Assets->load<T>(ref);
		}
		return source;
	}

	// ---- meshes ----------------------------------------------------------------------------------------------------------------

	const GpuMesh* GpuResourceCache::get_mesh(const AssetRef& mesh)
	{
		if (!mesh.valid())
		{
			return nullptr;
		}

		const AssetGuid guid = mesh.resolved();
		auto found = m_Meshes.find(guid);
		if (found == m_Meshes.end())
		{
			auto entry = std::make_unique<MeshEntry>();
			entry->Ref = mesh;
			entry->Data = acquire<MeshData>(mesh, m_InlineMeshes);
			m_PendingMeshes.push_back(entry.get());
			found = m_Meshes.emplace(guid, std::move(entry)).first;
		}
		return found->second->Uploaded ? &found->second->Mesh : nullptr;
	}

	std::optional<GpuResourceCache::Placement> GpuResourceCache::place(u64 vertexCount, u64 indexCount)
	{
		for (u32 pageIndex = 0; pageIndex < m_Pages.size(); pageIndex++)
		{
			Page& page = m_Pages[pageIndex];
			const std::optional<u64> vertices = page.VertexRanges.allocate(vertexCount);
			if (!vertices)
			{
				continue;
			}
			const std::optional<u64> indices = page.IndexRanges.allocate(indexCount);
			if (!indices)
			{
				page.VertexRanges.free(*vertices, vertexCount);
				continue;
			}
			return Placement{ pageIndex, *vertices, vertexCount, *indices, indexCount };
		}

		// No page has room: a new one, as large as the default or as the mesh needs.
		Page page;
		const u64 vertexCapacity = std::max(PAGE_VERTICES, vertexCount);
		const u64 indexCapacity = std::max(PAGE_INDICES, indexCount);

		nvrhi::BufferDesc vertexDescription;
		vertexDescription.byteSize = vertexCapacity * sizeof(MeshVertex);
		vertexDescription.isVertexBuffer = true;
		vertexDescription.initialState = UploadQueue::resting_state(m_Device);
		vertexDescription.keepInitialState = true;
		vertexDescription.debugName = "Mesh vertices";
		page.Vertex = m_Device->createBuffer(vertexDescription);

		nvrhi::BufferDesc indexDescription;
		indexDescription.byteSize = indexCapacity * sizeof(u32);
		indexDescription.isIndexBuffer = true;
		indexDescription.initialState = UploadQueue::resting_state(m_Device);
		indexDescription.keepInitialState = true;
		indexDescription.debugName = "Mesh indices";
		page.Index = m_Device->createBuffer(indexDescription);

		if (!page.Vertex || !page.Index)
		{
			WI_CACHE_LOG(Error, "could not allocate a mesh buffer page ({0} vertices, {1} indices)", vertexCapacity, indexCapacity);
			return std::nullopt;
		}

		page.VertexCapacity = vertexCapacity;
		page.VertexRanges = RangeAllocator(vertexCapacity);
		page.IndexRanges = RangeAllocator(indexCapacity);
		const u64 vertices = *page.VertexRanges.allocate(vertexCount);
		const u64 indices = *page.IndexRanges.allocate(indexCount);
		m_Pages.push_back(std::move(page));
		return Placement{ static_cast<u32>(m_Pages.size() - 1), vertices, vertexCount, indices, indexCount };
	}

	// A mesh that has joints and weights for every vertex (and says some primitive is skinned).
	bool GpuResourceCache::has_skin(const MeshData& data)
	{
		static_assert(sizeof(MeshSkinVertex) == 32, "the skin buffer holds MeshSkinVertex as is");
		return !data.SkinVertices.empty() && data.SkinVertices.size() == data.Vertices.size()
			&& std::any_of(data.Primitives.begin(), data.Primitives.end(), [](const MeshPrimitive& primitive) { return primitive.Skinned; });
	}

	bool GpuResourceCache::upload_mesh(MeshEntry& entry, const MeshData& data)
	{
		const std::optional<Placement> where = place(data.Vertices.size(), data.Indices.size());
		if (!where)
		{
			return false;
		}

		Page& page = m_Pages[where->Page];
		m_Uploads->write_buffer(page.Vertex, data.Vertices.data(), data.Vertices.size() * sizeof(MeshVertex), where->VertexOffset * sizeof(MeshVertex));
		m_Uploads->write_buffer(page.Index, data.Indices.data(), data.Indices.size() * sizeof(u32), where->IndexOffset * sizeof(u32));

		GpuMesh mesh;
		mesh.Page = where->Page;
		mesh.BoundsMin = data.BoundsMin;
		mesh.BoundsMax = data.BoundsMax;

		// the skin vertices go to the same place on the page's skin buffer, which exists once a skinned mesh is on the page
		if (has_skin(data))
		{
			if (page.Skin == nullptr)
			{
				nvrhi::BufferDesc skinDescription;
				skinDescription.byteSize = page.VertexCapacity * sizeof(MeshSkinVertex);
				skinDescription.structStride = sizeof(MeshSkinVertex);
				skinDescription.initialState = UploadQueue::resting_state(m_Device);
				skinDescription.keepInitialState = true;
				skinDescription.debugName = "Mesh skin vertices";
				page.Skin = m_Device->createBuffer(skinDescription);
			}
			if (page.Skin != nullptr)
			{
				m_Uploads->write_buffer(page.Skin, data.SkinVertices.data(), data.SkinVertices.size() * sizeof(MeshSkinVertex), where->VertexOffset * sizeof(MeshSkinVertex));
				mesh.Skinned = true;
				for (const MeshSkinVertex& vertex : data.SkinVertices)
				{
					for (int k = 0; k < 4; k++)
					{
						if (vertex.Weights[k] > 0.0f)
						{
							mesh.JointCount = std::max(mesh.JointCount, vertex.Joints[k] + 1);
						}
					}
				}
			}
		}
		for (const MeshPrimitive& primitive : data.Primitives)
		{
			GpuPrimitive gpu;
			gpu.FirstIndex = static_cast<u32>(where->IndexOffset) + primitive.FirstIndex;
			gpu.IndexCount = primitive.IndexCount;
			gpu.BaseVertex = static_cast<i32>(where->VertexOffset) + primitive.BaseVertex;
			gpu.MaterialSlot = get_material_slot(primitive.Material);
			gpu.BoundsMin = primitive.BoundsMin;
			gpu.BoundsMax = primitive.BoundsMax;
			mesh.Primitives.push_back(gpu);
		}

		if (entry.Uploaded)
		{
			m_Retired.push_back({ entry.Where, m_Frame });
		}
		entry.Mesh = std::move(mesh);
		entry.Where = *where;
		entry.Uploaded = true;
		return true;
	}

	void GpuResourceCache::update_meshes()
	{
		// The list can grow while it is walked (uploading a mesh asks for its materials, not for meshes), so index into it.
		for (size_t i = 0; i < m_PendingMeshes.size();)
		{
			MeshEntry& entry = *m_PendingMeshes[i];
			const LoadState state = entry.Data.state();
			bool finished = false;

			if (state == LoadState::Failed)
			{
				if (!entry.Uploaded)
				{
					entry.Failed = true;
					m_FailedCount++;
					WI_CACHE_LOG(Error, "mesh {0} failed to load, its instances are not drawn", entry.Ref.Guid.to_string());
				}
				else
				{
					WI_CACHE_LOG(Warning, "mesh {0} could not be reloaded, the old version stays", entry.Ref.Guid.to_string());
				}
				entry.Reloading = false;
				finished = true;
			}
			else if (state == LoadState::Ready)
			{
				const MeshData* data = entry.Data.get();
				if (!valid_mesh(*data))
				{
					if (!entry.Uploaded)
					{
						entry.Failed = true;
						m_FailedCount++;
					}
					WI_CACHE_LOG(Error, "mesh {0} holds no usable geometry", entry.Ref.Guid.to_string());
					finished = true;
				}
				else
				{
					const size_t bytes = data->Vertices.size() * sizeof(MeshVertex) + data->Indices.size() * sizeof(u32)
						+ (has_skin(*data) ? data->SkinVertices.size() * sizeof(MeshSkinVertex) : 0);
					if (m_Uploads->has_room(bytes))
					{
						if (upload_mesh(entry, *data))
						{
							if (entry.Reloading)
							{
								m_Reloads++;
							}
						}
						else if (!entry.Uploaded)
						{
							entry.Failed = true;
							m_FailedCount++;
						}
						entry.Reloading = false;
						finished = true;
					}
				}
			}

			if (finished)
			{
				entry.Data.reset(); // the CPU copy can go
				m_PendingMeshes.erase(m_PendingMeshes.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}
	}

	void GpuResourceCache::release_retired()
	{
		for (size_t i = 0; i < m_Retired.size();)
		{
			if (m_Retired[i].Frame + RETIRE_FRAMES <= m_Frame)
			{
				const Placement& where = m_Retired[i].Where;
				m_Pages[where.Page].VertexRanges.free(where.VertexOffset, where.VertexCount);
				m_Pages[where.Page].IndexRanges.free(where.IndexOffset, where.IndexCount);
				m_Retired.erase(m_Retired.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}

		for (size_t i = 0; i < m_RetiredParams.size();)
		{
			if (m_RetiredParams[i].Frame + RETIRE_FRAMES <= m_Frame)
			{
				m_ParamRanges.free(m_RetiredParams[i].Offset, m_RetiredParams[i].Size);
				m_RetiredParams.erase(m_RetiredParams.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}
	}

	// ---- textures --------------------------------------------------------------------------------------------------------------

	GpuResourceCache::TextureEntry& GpuResourceCache::texture_entry(const AssetRef& ref)
	{
		const AssetGuid guid = ref.resolved();
		auto found = m_Textures.find(guid);
		if (found == m_Textures.end())
		{
			auto entry = std::make_unique<TextureEntry>();
			entry->Ref = ref;
			entry->Data = acquire<TextureData>(ref, m_InlineTextures);
			m_PendingTextures.push_back(entry.get());
			found = m_Textures.emplace(guid, std::move(entry)).first;
		}
		return *found->second;
	}

	bool GpuResourceCache::upload_texture(TextureEntry& entry, const TextureData& data)
	{
		nvrhi::TextureDesc description;
		description.width = data.Width;
		description.height = data.Height;
		description.mipLevels = static_cast<u32>(data.Mips.size());
		description.format = texture_format(data.Kind);
		description.isShaderResource = true;
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "Material texture";
		nvrhi::TextureHandle texture = m_Device->createTexture(description);
		if (!texture)
		{
			return false;
		}

		for (u32 mip = 0; mip < data.Mips.size(); mip++)
		{
			const size_t width = std::max(data.Width >> mip, 1u);
			m_Uploads->write_texture(texture, 0, mip, data.Mips[mip].data(), width * 4);
		}

		entry.Texture = std::move(texture); // the old one, if any, lives on in the frames that still use it
		entry.Bytes = texture_bytes(data);
		return true;
	}

	void GpuResourceCache::update_textures()
	{
		for (size_t i = 0; i < m_PendingTextures.size();)
		{
			TextureEntry& entry = *m_PendingTextures[i];
			const LoadState state = entry.Data.state();
			bool finished = false;
			bool uploaded = false;

			if (state == LoadState::Failed)
			{
				if (!entry.Texture)
				{
					entry.Failed = true;
					m_FailedCount++;
					WI_CACHE_LOG(Warning, "texture {0} failed to load, materials use the fallback", entry.Ref.Guid.to_string());
				}
				else
				{
					WI_CACHE_LOG(Warning, "texture {0} could not be reloaded, the old version stays", entry.Ref.Guid.to_string());
				}
				entry.Reloading = false;
				finished = true;
			}
			else if (state == LoadState::Ready)
			{
				const TextureData* data = entry.Data.get();
				if (!valid_texture(*data))
				{
					if (!entry.Texture)
					{
						entry.Failed = true;
						m_FailedCount++;
					}
					WI_CACHE_LOG(Warning, "texture {0} holds no usable pixels, materials use the fallback", entry.Ref.Guid.to_string());
					entry.Reloading = false;
					finished = true;
				}
				else if (m_Uploads->has_room(texture_bytes(*data)))
				{
					if (upload_texture(entry, *data))
					{
						uploaded = true;
						if (entry.Reloading)
						{
							m_Reloads++;
						}
					}
					else if (!entry.Texture)
					{
						entry.Failed = true;
						m_FailedCount++;
					}
					entry.Reloading = false;
					finished = true;
				}
			}

			if (!finished)
			{
				i++;
				continue;
			}

			entry.Data.reset();
			if (uploaded || entry.Failed)
			{
				// Materials that use it build their binding set again.
				const AssetGuid guid = entry.Ref.resolved();
				const auto uses = [&](const AssetRef& used) { return used.valid() && used.resolved() == guid; };
				for (auto& [materialGuid, material] : m_MaterialEntries)
				{
					if (std::any_of(std::begin(material->Textures), std::end(material->Textures), uses))
					{
						for (nvrhi::ITexture*& bound : material->Bound)
						{
							bound = nullptr;
						}
						material->Settled = false;
						push_unique(m_PendingMaterials, material.get());
					}
					if (std::any_of(std::begin(material->ParamTextures), std::end(material->ParamTextures), uses))
					{
						for (nvrhi::ITexture*& bound : material->ParamBound)
						{
							bound = nullptr;
						}
						material->Settled = false;
						push_unique(m_PendingMaterials, material.get());
					}
				}
			}
			m_PendingTextures.erase(m_PendingTextures.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	nvrhi::ITexture* GpuResourceCache::get_texture(const AssetRef& texture)
	{
		return texture.valid() ? texture_entry(texture).Texture.Get() : nullptr;
	}

	// ---- fonts -----------------------------------------------------------------------------------------------------------------

	GpuResourceCache::FontEntry& GpuResourceCache::font_entry(const AssetRef& ref)
	{
		const AssetGuid guid = ref.resolved();
		auto found = m_Fonts.find(guid);
		if (found == m_Fonts.end())
		{
			auto entry = std::make_unique<FontEntry>();
			entry->Ref = ref;
			entry->Data = acquire<FontData>(ref, m_InlineFonts);
			m_PendingFonts.push_back(entry.get());
			found = m_Fonts.emplace(guid, std::move(entry)).first;
		}
		return *found->second;
	}

	bool GpuResourceCache::upload_font(FontEntry& entry, const FontData& data)
	{
		nvrhi::TextureDesc description;
		description.width = data.AtlasWidth;
		description.height = data.AtlasHeight;
		description.format = nvrhi::Format::R8_UNORM;
		description.isShaderResource = true;
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "Font atlas";
		nvrhi::TextureHandle atlas = m_Device->createTexture(description);
		if (!atlas)
		{
			return false;
		}

		m_Uploads->write_texture(atlas, 0, 0, data.Atlas.data(), data.AtlasWidth);
		entry.Atlas = std::move(atlas); // the old one, if any, lives on in the frames that still use it
		entry.Bytes = data.Atlas.size();
		return true;
	}

	void GpuResourceCache::update_fonts()
	{
		for (size_t i = 0; i < m_PendingFonts.size();)
		{
			FontEntry& entry = *m_PendingFonts[i];
			const LoadState state = entry.Data.state();
			bool finished = false;

			if (state == LoadState::Failed)
			{
				if (!entry.Atlas)
				{
					entry.Failed = true;
					m_FailedCount++;
					WI_CACHE_LOG(Warning, "font {0} failed to load, its text is not drawn", entry.Ref.Guid.to_string());
				}
				else
				{
					WI_CACHE_LOG(Warning, "font {0} could not be reloaded, the old version stays", entry.Ref.Guid.to_string());
				}
				entry.Reloading = false;
				finished = true;
			}
			else if (state == LoadState::Ready)
			{
				const FontData* data = entry.Data.get();
				const bool usable = data->AtlasWidth > 0 && data->AtlasHeight > 0 && data->Atlas.size() == static_cast<size_t>(data->AtlasWidth) * data->AtlasHeight;
				if (!usable)
				{
					if (!entry.Atlas)
					{
						entry.Failed = true;
						m_FailedCount++;
					}
					WI_CACHE_LOG(Warning, "font {0} has no usable atlas, its text is not drawn", entry.Ref.Guid.to_string());
					entry.Reloading = false;
					finished = true;
				}
				else if (m_Uploads->has_room(data->Atlas.size()))
				{
					if (upload_font(entry, *data))
					{
						if (entry.Reloading)
						{
							m_Reloads++;
						}
					}
					else if (!entry.Atlas)
					{
						entry.Failed = true;
						m_FailedCount++;
					}
					entry.Reloading = false;
					finished = true;
				}
			}

			if (!finished)
			{
				i++;
				continue;
			}

			entry.Data.reset();
			m_PendingFonts.erase(m_PendingFonts.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	nvrhi::ITexture* GpuResourceCache::get_font_atlas(const AssetRef& font)
	{
		return font.valid() ? font_entry(font).Atlas.Get() : nullptr;
	}

	// ---- materials -------------------------------------------------------------------------------------------------------------

	u32 GpuResourceCache::get_material_slot(const AssetRef& material)
	{
		if (!material.valid())
		{
			return 0;
		}

		const AssetGuid guid = material.resolved();
		const auto found = m_MaterialEntries.find(guid);
		if (found != m_MaterialEntries.end())
		{
			return found->second->Slot;
		}

		auto entry = std::make_unique<MaterialEntry>();
		entry->Ref = material;
		entry->Data = acquire<MaterialData>(material, m_InlineMaterials);
		entry->Slot = static_cast<u32>(m_Records.size());
		m_Records.push_back(GpuMaterialRecord{});
		m_Bindings.push_back(m_FallbackBindings);
		m_SlotShaders.push_back(0);
		m_CustomRecords.push_back(GpuCustomRecord{});
		m_CustomBindings.push_back(m_FallbackCustomBindings);
		ensure_material_capacity(static_cast<u32>(m_Records.size()));
		m_DirtyRecords.push_back(entry->Slot);
		m_PendingMaterials.push_back(entry.get());
		const u32 slot = entry->Slot;
		m_MaterialEntries.emplace(guid, std::move(entry));
		return slot;
	}

	nvrhi::IBindingSet* GpuResourceCache::get_material_bindings(u32 slot)
	{
		return slot < m_Bindings.size() ? m_Bindings[slot].Get() : m_FallbackBindings.Get();
	}

	nvrhi::IBindingSet* GpuResourceCache::get_custom_bindings(u32 slot)
	{
		return slot < m_CustomBindings.size() && m_CustomBindings[slot] ? m_CustomBindings[slot].Get() : m_FallbackCustomBindings.Get();
	}

	void GpuResourceCache::ensure_material_capacity(u32 slots)
	{
		if (slots <= m_MaterialCapacity)
		{
			return;
		}

		u32 capacity = std::max(INITIAL_MATERIAL_CAPACITY, m_MaterialCapacity);
		while (capacity < slots)
		{
			capacity *= 2;
		}

		nvrhi::BufferDesc description;
		description.byteSize = static_cast<u64>(capacity) * sizeof(GpuMaterialRecord);
		description.structStride = sizeof(GpuMaterialRecord);
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "Material records";
		m_MaterialBuffer = m_Device->createBuffer(description);

		nvrhi::BufferDesc customDescription;
		customDescription.byteSize = static_cast<u64>(capacity) * sizeof(GpuCustomRecord);
		customDescription.structStride = sizeof(GpuCustomRecord);
		customDescription.initialState = UploadQueue::resting_state(m_Device);
		customDescription.keepInitialState = true;
		customDescription.debugName = "Custom material records";
		m_CustomBuffer = m_Device->createBuffer(customDescription);

		m_MaterialCapacity = capacity;
		m_MaterialGeneration++;
		rebuild_custom_bindings();

		// A new buffer holds nothing: every record is written again.
		m_DirtyRecords.clear();
		for (u32 slot = 0; slot < m_Records.size(); slot++)
		{
			m_DirtyRecords.push_back(slot);
		}
	}

	void GpuResourceCache::write_material_record(u32 slot)
	{
		m_Uploads->write_buffer(m_MaterialBuffer, &m_Records[slot], sizeof(GpuMaterialRecord), static_cast<u64>(slot) * sizeof(GpuMaterialRecord));
		m_Uploads->write_buffer(m_CustomBuffer, &m_CustomRecords[slot], sizeof(GpuCustomRecord), static_cast<u64>(slot) * sizeof(GpuCustomRecord));
	}

	// ---- custom materials ------------------------------------------------------------------------------------------------------

	// The binding sets name the buffers: a new buffer means new sets (with the textures each set had).
	void GpuResourceCache::rebuild_custom_bindings()
	{
		if (!m_CustomBuffer || !m_ParamBuffer)
		{
			return; // the constructor makes them once both buffers exist
		}

		nvrhi::ITexture* const none[SURFACE_MAX_TEXTURES]{};
		m_FallbackCustomBindings = make_custom_bindings(none);
		for (nvrhi::BindingSetHandle& set : m_CustomBindings)
		{
			set = m_FallbackCustomBindings;
		}
		for (auto& [guid, entry] : m_MaterialEntries)
		{
			if (entry->ShaderIndex != 0)
			{
				m_CustomBindings[entry->Slot] = make_custom_bindings(entry->ParamBound);
			}
		}
	}

	void GpuResourceCache::release_params(MaterialEntry& entry)
	{
		if (entry.ParamCapacity != 0)
		{
			// Frames in flight may still read the block: it is reused later.
			m_RetiredParams.push_back({ entry.ParamOffset, entry.ParamCapacity, m_Frame });
		}
		entry.ParamOffset = 0;
		entry.ParamCapacity = 0;
	}

	// A larger buffer: every block is placed again (packed from 0) and written, the records follow. The old buffer lives on in
	// the frames that still use it.
	void GpuResourceCache::grow_param_buffer(u64 atLeast)
	{
		u64 capacity = std::max(INITIAL_PARAM_BYTES, m_ParamRanges.capacity() * 2);
		while (capacity < atLeast)
		{
			capacity *= 2;
		}

		nvrhi::BufferDesc description;
		description.byteSize = capacity;
		description.canHaveRawViews = true;
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "Custom material parameters";
		nvrhi::BufferHandle buffer = m_Device->createBuffer(description);
		if (!buffer)
		{
			WI_CACHE_LOG(Error, "could not allocate the material parameter buffer ({0} bytes)", capacity);
			return;
		}

		m_ParamBuffer = std::move(buffer);
		m_ParamRanges = RangeAllocator(capacity);
		m_RetiredParams.clear();
		for (auto& [guid, entry] : m_MaterialEntries)
		{
			if (entry->ParamCapacity == 0)
			{
				continue;
			}
			entry->ParamOffset = *m_ParamRanges.allocate(entry->ParamCapacity); // fits: the new buffer holds at least twice as much
			write_params(*entry);
			if (m_SlotShaders[entry->Slot] != 0)
			{
				m_CustomRecords[entry->Slot].ParamOffset = static_cast<u32>(entry->ParamOffset);
				m_DirtyRecords.push_back(entry->Slot);
			}
		}
		m_MaterialGeneration++;
		rebuild_custom_bindings();
	}

	bool GpuResourceCache::place_params(MaterialEntry& entry, u64 bytes)
	{
		if (entry.ParamCapacity == bytes)
		{
			return true;
		}
		release_params(entry);
		if (bytes == 0)
		{
			return true;
		}

		std::optional<u64> offset = m_ParamRanges.allocate(bytes);
		if (!offset)
		{
			grow_param_buffer(m_ParamRanges.used() + bytes);
			offset = m_ParamRanges.allocate(bytes);
		}
		if (!offset)
		{
			return false;
		}
		entry.ParamOffset = *offset;
		entry.ParamCapacity = bytes;
		return true;
	}

	// The block as the shader reads it: the material's bytes, zeros after them (a block cooked for another layout).
	void GpuResourceCache::write_params(const MaterialEntry& entry)
	{
		if (entry.ParamCapacity == 0)
		{
			return;
		}
		std::vector<u8> block(entry.ParamCapacity, 0);
		std::memcpy(block.data(), entry.ParamBytes.data(), std::min<size_t>(entry.ParamBytes.size(), block.size()));
		m_Uploads->write_buffer(m_ParamBuffer, block.data(), block.size(), entry.ParamOffset);
	}

	// What the slot shows given its data and its shader's state: the built-in record (no shader), the built-in record with the
	// custom bit and a custom record (shader ready), the default record (shader loading) or magenta (shader failed).
	void GpuResourceCache::compose_record(MaterialEntry& entry)
	{
		const GpuSurfaceShader* shader = m_SurfaceShaders->get(entry.ShaderIndex);
		GpuMaterialRecord record = entry.Builtin;
		GpuCustomRecord custom{};
		u32 drawShader = 0;

		if (entry.ShaderIndex == 0)
		{
			release_params(entry);
		}
		else if (shader == nullptr || shader->Loading)
		{
			record = GpuMaterialRecord{};
			release_params(entry);
		}
		else if (shader->Failed)
		{
			record = magenta_record();
			release_params(entry);
		}
		else
		{
			if (entry.ParamBytes.size() != shader->BlockSize && !entry.WarnedParamSize)
			{
				entry.WarnedParamSize = true;
				WI_CACHE_LOG(Warning, "material {0} has {1} parameter bytes, its shader reads {2} (cooked for another version of the shader?)",
					entry.Ref.Guid.to_string(), entry.ParamBytes.size(), shader->BlockSize);
			}
			if (place_params(entry, align_param_size(shader->BlockSize)))
			{
				write_params(entry);
				record.Flags |= MATERIAL_CUSTOM_BIT;
				custom.ParamOffset = static_cast<u32>(entry.ParamOffset);
				custom.ParamSize = shader->BlockSize;
				drawShader = entry.ShaderIndex;
			}
			else
			{
				record = magenta_record();
			}
		}

		m_Records[entry.Slot] = record;
		m_CustomRecords[entry.Slot] = custom;
		m_SlotShaders[entry.Slot] = drawShader;
		m_DirtyRecords.push_back(entry.Slot);
	}

	// The custom binding set follows the @param textures that have arrived.
	void GpuResourceCache::update_custom_bindings(MaterialEntry& entry, bool force, bool& settled)
	{
		if (entry.ShaderIndex == 0)
		{
			m_CustomBindings[entry.Slot] = m_FallbackCustomBindings;
			return;
		}

		nvrhi::ITexture* textures[SURFACE_MAX_TEXTURES]{};
		bool changed = force;
		for (u32 t = 0; t < SURFACE_MAX_TEXTURES; t++)
		{
			textures[t] = texture_or_fallback(entry.ParamTextures[t], 0);
			changed = changed || textures[t] != entry.ParamBound[t];
			if (entry.ParamTextures[t].valid())
			{
				const TextureEntry& texture = texture_entry(entry.ParamTextures[t]);
				settled = settled && (texture.Texture || texture.Failed);
			}
		}
		if (changed)
		{
			for (u32 t = 0; t < SURFACE_MAX_TEXTURES; t++)
			{
				entry.ParamBound[t] = textures[t];
			}
			m_CustomBindings[entry.Slot] = make_custom_bindings(textures);
		}
	}

	void GpuResourceCache::build_record(MaterialEntry& entry, const MaterialData& data)
	{
		GpuMaterialRecord record;
		record.BaseColor = data.BaseColorFactor;
		record.Emissive = data.EmissiveFactor;
		record.Metallic = data.MetallicFactor;
		record.Roughness = data.RoughnessFactor;
		record.AlphaCutoff = data.AlphaCutoff;
		record.Flags = (data.Alpha == AlphaMode::Mask ? MATERIAL_ALPHA_MASK_BIT : 0) | (data.Alpha == AlphaMode::Blend ? MATERIAL_ALPHA_BLEND_BIT : 0)
			| (data.DoubleSided ? MATERIAL_DOUBLE_SIDED_BIT : 0);
		record.Extra = glm::vec4(data.Clearcoat, data.ClearcoatRoughness, data.SheenIntensity, data.Transmission);
		entry.Builtin = record;

		entry.Textures[0] = data.BaseColorTexture;
		entry.Textures[1] = data.NormalTexture;
		entry.Textures[2] = data.OrmTexture;
		entry.Textures[3] = data.EmissiveTexture;

		entry.ShaderIndex = m_SurfaceShaders->get_shader_index(data.Shader);
		entry.ParamBytes = data.ParamBytes;
		entry.WarnedParamSize = false;
		for (u32 t = 0; t < SURFACE_MAX_TEXTURES; t++)
		{
			entry.ParamTextures[t] = data.ParamTextures[t];
			entry.ParamBound[t] = nullptr;
		}
		compose_record(entry);
	}

	nvrhi::ITexture* GpuResourceCache::texture_or_fallback(const AssetRef& ref, u32 index)
	{
		if (ref.valid())
		{
			TextureEntry& texture = texture_entry(ref);
			if (texture.Texture)
			{
				return texture.Texture;
			}
		}
		return m_FallbackTextures[index];
	}

	void GpuResourceCache::update_materials()
	{
		for (size_t i = 0; i < m_PendingMaterials.size();)
		{
			MaterialEntry& entry = *m_PendingMaterials[i];
			bool rebuiltCustom = false;

			if (!entry.HasData)
			{
				const LoadState state = entry.Data.state();
				if (state == LoadState::Loading)
				{
					i++;
					continue;
				}

				if (state == LoadState::Failed || entry.Data.get() == nullptr)
				{
					if (!entry.Failed)
					{
						entry.Failed = true;
						m_FailedCount++;
						WI_CACHE_LOG(Error, "material {0} failed to load, it shows magenta", entry.Ref.Guid.to_string());
					}
					entry.Builtin = magenta_record();
					entry.ShaderIndex = 0;
					entry.ParamBytes.clear();
					compose_record(entry); // magenta, no shader
					m_CustomBindings[entry.Slot] = m_FallbackCustomBindings;
					entry.Data.reset();
					m_PendingMaterials.erase(m_PendingMaterials.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}

				if (entry.Failed)
				{
					entry.Failed = false; // a re-import fixed it
					m_FailedCount--;
				}
				build_record(entry, *entry.Data.get());
				entry.HasData = true;
				entry.Reloading = false;
				entry.Data.reset();
				for (nvrhi::ITexture*& bound : entry.Bound)
				{
					bound = nullptr;
				}
				rebuiltCustom = true;
			}

			// The binding set follows what has arrived.
			nvrhi::ITexture* textures[4]{};
			bool changed = false;
			bool settled = true;
			u32 mask = 0;
			for (u32 t = 0; t < 4; t++)
			{
				textures[t] = texture_or_fallback(entry.Textures[t], t);
				changed = changed || textures[t] != entry.Bound[t];
				if (entry.Textures[t].valid())
				{
					const TextureEntry& texture = texture_entry(entry.Textures[t]);
					settled = settled && (texture.Texture || texture.Failed);
					mask |= texture.Texture ? (1u << t) : 0u;
				}
			}

			if (changed)
			{
				m_Bindings[entry.Slot] = make_bindings(textures);
				for (u32 t = 0; t < 4; t++)
				{
					entry.Bound[t] = textures[t];
				}
				if (entry.Builtin.TextureMask != mask)
				{
					entry.Builtin.TextureMask = mask;
					compose_record(entry);
				}
			}
			update_custom_bindings(entry, rebuiltCustom, settled);

			if (settled)
			{
				entry.Settled = true;
				m_PendingMaterials.erase(m_PendingMaterials.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}
	}

	// ---- frame -----------------------------------------------------------------------------------------------------------------

	void GpuResourceCache::update()
	{
		m_Frame++;
		release_retired();

		if (!m_FallbacksUploaded)
		{
			const u8 pixels[4][4] = { { 255, 255, 255, 255 }, { 128, 128, 255, 255 }, { 255, 128, 0, 255 }, { 0, 0, 0, 255 } };
			for (u32 i = 0; i < 4; i++)
			{
				m_Uploads->write_texture(m_FallbackTextures[i], 0, 0, pixels[i], 4);
			}
			m_FallbacksUploaded = true;
		}

		m_SurfaceShaders->update();
		update_textures();
		update_fonts();
		update_meshes();
		update_materials();

		// A surface shader appeared, was replaced or failed: the materials that use one show what their shader is now.
		if (m_SurfaceShaders->generation() != m_SurfaceGeneration)
		{
			m_SurfaceGeneration = m_SurfaceShaders->generation();
			for (auto& [guid, entry] : m_MaterialEntries)
			{
				if (entry->HasData && entry->ShaderIndex != 0)
				{
					compose_record(*entry);
				}
			}
		}

		if (!m_DirtyRecords.empty())
		{
			std::sort(m_DirtyRecords.begin(), m_DirtyRecords.end());
			m_DirtyRecords.erase(std::unique(m_DirtyRecords.begin(), m_DirtyRecords.end()), m_DirtyRecords.end());
			for (const u32 slot : m_DirtyRecords)
			{
				write_material_record(slot);
			}
			m_DirtyRecords.clear();
		}
	}

	void GpuResourceCache::invalidate(AssetGuid resolved)
	{
		m_SurfaceShaders->invalidate(resolved);

		if (const auto effect = m_Effects.find(resolved); effect != m_Effects.end())
		{
			effect->second.Data = acquire<ParticleEffectData>(AssetRef{ resolved, 0 }, m_InlineEffects);
			effect->second.Revision++;
		}

		if (const auto mesh = m_Meshes.find(resolved); mesh != m_Meshes.end())
		{
			MeshEntry& entry = *mesh->second;
			entry.Data = acquire<MeshData>(entry.Ref, m_InlineMeshes);
			entry.Reloading = entry.Uploaded;
			if (entry.Failed)
			{
				entry.Failed = false;
				m_FailedCount--;
			}
			push_unique(m_PendingMeshes, &entry);
		}

		if (const auto texture = m_Textures.find(resolved); texture != m_Textures.end())
		{
			TextureEntry& entry = *texture->second;
			entry.Data = acquire<TextureData>(entry.Ref, m_InlineTextures);
			entry.Reloading = entry.Texture != nullptr;
			if (entry.Failed)
			{
				entry.Failed = false;
				m_FailedCount--;
			}
			push_unique(m_PendingTextures, &entry);
		}

		if (const auto font = m_Fonts.find(resolved); font != m_Fonts.end())
		{
			FontEntry& entry = *font->second;
			entry.Data = acquire<FontData>(entry.Ref, m_InlineFonts);
			entry.Reloading = entry.Atlas != nullptr;
			if (entry.Failed)
			{
				entry.Failed = false;
				m_FailedCount--;
			}
			push_unique(m_PendingFonts, &entry);
		}

		if (const auto material = m_MaterialEntries.find(resolved); material != m_MaterialEntries.end())
		{
			MaterialEntry& entry = *material->second;
			entry.Data = acquire<MaterialData>(entry.Ref, m_InlineMaterials);
			entry.HasData = false;
			entry.Reloading = true;
			entry.Settled = false;
			push_unique(m_PendingMaterials, &entry);
		}
	}

	// ---- inline assets and stats -------------------------------------------------------------------------------------------------

	AssetRef GpuResourceCache::add_inline_mesh(MeshData mesh)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineMeshes[ref.resolved()] = std::make_shared<const MeshData>(std::move(mesh));
		return ref;
	}

	AssetRef GpuResourceCache::add_inline_material(MaterialData material)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineMaterials[ref.resolved()] = std::make_shared<const MaterialData>(std::move(material));
		return ref;
	}

	AssetRef GpuResourceCache::add_inline_particle_effect(ParticleEffectData effect)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineEffects[ref.resolved()] = std::make_shared<const ParticleEffectData>(std::move(effect));
		return ref;
	}

	void GpuResourceCache::set_inline_particle_effect(const AssetRef& effect, ParticleEffectData data)
	{
		m_InlineEffects[effect.resolved()] = std::make_shared<const ParticleEffectData>(std::move(data));
		invalidate(effect.resolved());
	}

	const ParticleEffectData* GpuResourceCache::get_particle_effect(const AssetRef& effect)
	{
		if (!effect.valid())
		{
			return nullptr;
		}
		auto found = m_Effects.find(effect.resolved());
		if (found == m_Effects.end())
		{
			found = m_Effects.emplace(effect.resolved(), EffectEntry{}).first;
			found->second.Data = acquire<ParticleEffectData>(effect, m_InlineEffects);
		}
		return found->second.Data.state() == LoadState::Ready ? found->second.Data.get() : nullptr;
	}

	u32 GpuResourceCache::effect_revision(const AssetRef& effect) const
	{
		const auto found = m_Effects.find(effect.resolved());
		return found == m_Effects.end() ? 0u : found->second.Revision;
	}

	AssetRef GpuResourceCache::add_inline_texture(TextureData texture)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineTextures[ref.resolved()] = std::make_shared<const TextureData>(std::move(texture));
		return ref;
	}

	AssetRef GpuResourceCache::add_inline_font(FontData font)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineFonts[ref.resolved()] = std::make_shared<const FontData>(std::move(font));
		return ref;
	}

	void GpuResourceCache::set_inline_texture(const AssetRef& texture, TextureData data)
	{
		m_InlineTextures[texture.resolved()] = std::make_shared<const TextureData>(std::move(data));
	}

	void GpuResourceCache::set_inline_mesh(const AssetRef& mesh, MeshData data)
	{
		m_InlineMeshes[mesh.resolved()] = std::make_shared<const MeshData>(std::move(data));
	}

	void GpuResourceCache::set_inline_material(const AssetRef& material, MaterialData data)
	{
		m_InlineMaterials[material.resolved()] = std::make_shared<const MaterialData>(std::move(data));
	}

	GpuResourceCache::Stats GpuResourceCache::get_stats() const
	{
		Stats stats;
		for (const auto& [guid, entry] : m_Meshes)
		{
			stats.Meshes += entry->Uploaded ? 1 : 0;
		}
		for (const auto& [guid, entry] : m_Textures)
		{
			stats.Textures += entry->Texture ? 1 : 0;
			stats.Bytes += entry->Bytes;
		}
		for (const Page& page : m_Pages)
		{
			stats.Bytes += page.VertexRanges.used() * sizeof(MeshVertex) + page.IndexRanges.used() * sizeof(u32);
		}
		stats.Materials = static_cast<u32>(m_Records.size());
		stats.Pending = static_cast<u32>(m_PendingMeshes.size() + m_PendingTextures.size() + m_PendingFonts.size() + m_PendingMaterials.size());
		stats.Failed = m_FailedCount;
		stats.Reloads = m_Reloads;
		return stats;
	}
}
