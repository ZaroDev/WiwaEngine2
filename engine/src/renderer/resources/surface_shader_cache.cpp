#include <pch.h>
#include "surface_shader_cache.h"

#define WI_SURFACE_LOG(level, ...) WI_LOG(level, "Shaders", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		const char* variant_name(u32 kind)
		{
			constexpr const char* NAMES[SURFACE_VARIANT_COUNT] = { "forward", "depth", "depth masked", "velocity", "shadow", "shadow masked" };
			return kind < SURFACE_VARIANT_COUNT ? NAMES[kind] : "unknown";
		}
	}

	SurfaceShaderCache::SurfaceShaderCache(nvrhi::IDevice* device, GraphicsApi api, AssetManager* assets)
		: m_Device(device), m_Api(api), m_Assets(assets)
	{
	}

	SurfaceShaderCache::~SurfaceShaderCache() = default;

	void SurfaceShaderCache::start_load(Entry& entry)
	{
		entry.HasPending = true;
		entry.Pending = {};
		entry.PendingInline.reset();
		if (const auto found = m_Inline.find(entry.Asset.resolved()); found != m_Inline.end())
		{
			entry.PendingInline = found->second;
		}
		else if (m_Assets != nullptr)
		{
			entry.Pending = m_Assets->load<SurfaceShaderData>(entry.Asset);
		}
	}

	u32 SurfaceShaderCache::get_shader_index(const AssetRef& shader)
	{
		if (!shader.valid())
		{
			return 0;
		}

		const AssetGuid guid = shader.resolved();
		if (const auto found = m_Indices.find(guid); found != m_Indices.end())
		{
			return found->second;
		}

		auto entry = std::make_unique<Entry>();
		entry->Asset = shader;
		start_load(*entry);
		m_Entries.push_back(std::move(entry));
		const u32 index = static_cast<u32>(m_Entries.size());
		m_Indices.emplace(guid, index);
		return index;
	}

	const GpuSurfaceShader* SurfaceShaderCache::get(u32 index) const
	{
		return index >= 1 && index <= m_Entries.size() ? &m_Entries[index - 1]->Gpu : nullptr;
	}

	void SurfaceShaderCache::update()
	{
		for (const std::unique_ptr<Entry>& owned : m_Entries)
		{
			Entry& entry = *owned;
			if (!entry.HasPending)
			{
				continue;
			}

			if (entry.PendingInline)
			{
				finish(entry, entry.PendingInline.get());
				continue;
			}

			switch (entry.Pending.state())
			{
			case LoadState::Loading:
				break;
			case LoadState::Failed:
				finish(entry, nullptr);
				break;
			case LoadState::Ready:
				finish(entry, entry.Pending.get());
				break;
			}
		}
	}

	void SurfaceShaderCache::finish(Entry& entry, const SurfaceShaderData* data)
	{
		const std::string name = data != nullptr ? data->Name : entry.Asset.Guid.to_string();
		const bool hadVersion = !entry.Gpu.Loading && !entry.Gpu.Failed;

		GpuSurfaceShader built;
		bool usable = data != nullptr;
		if (data == nullptr)
		{
			WI_SURFACE_LOG(Error, "Surface shader {0} could not be loaded{1}", name, hadVersion ? ", keeping the previous version" : "");
		}
		else
		{
			const ShaderTarget target = (m_Api == GraphicsApi::Vulkan) ? ShaderTarget::Spirv : ShaderTarget::Dxbc;
			for (u32 kind = 0; kind < SURFACE_VARIANT_COUNT && usable; kind++)
			{
				for (u32 stageIndex = 0; stageIndex < 2 && usable; stageIndex++)
				{
					const ShaderStage stage = stageIndex == 0 ? ShaderStage::Vertex : ShaderStage::Pixel;
					const SurfaceVariant* variant = data->find(static_cast<SurfaceVariantKind>(kind), stage, target);
					if (variant == nullptr || variant->Code.empty())
					{
						// An incomplete asset (a pak cooked without this backend, say) is a failed shader.
						WI_SURFACE_LOG(Error, "Surface shader {0} has no {1} {2} {3} binary", name, variant_name(kind), stageIndex == 0 ? "vertex" : "pixel",
							target == ShaderTarget::Spirv ? "SPIR-V" : "DXBC");
						usable = false;
						break;
					}

					const nvrhi::ShaderDesc description = nvrhi::ShaderDesc()
						.setShaderType(stage == ShaderStage::Vertex ? nvrhi::ShaderType::Vertex : nvrhi::ShaderType::Pixel)
						.setEntryName(stage == ShaderStage::Vertex ? "VSMain" : "PSMain")
						.setDebugName(name);
					built.Shaders[kind][stageIndex] = m_Device->createShader(description, variant->Code.data(), variant->Code.size());
					if (built.Shaders[kind][stageIndex] == nullptr)
					{
						WI_SURFACE_LOG(Error, "Surface shader {0}: the device rejected the {1} {2} shader", name, variant_name(kind), stageIndex == 0 ? "vertex" : "pixel");
						usable = false;
					}
				}
			}
		}

		if (usable)
		{
			built.TextureCount = static_cast<u32>(std::min<size_t>(data->Textures.size(), SURFACE_MAX_TEXTURES));
			built.BlockSize = data->BlockSize;
			built.Failed = false;
			built.Loading = false;
			built.Generation = entry.Gpu.Generation + 1;
			entry.Gpu = std::move(built);
			m_Generation++;
			WI_SURFACE_LOG(Info, "Surface shader {0} ready", name);
		}
		else if (!hadVersion)
		{
			// Nothing to keep: the shader is failed (its materials turn magenta).
			if (!entry.Gpu.Failed)
			{
				entry.Gpu.Failed = true;
				entry.Gpu.Loading = false;
				m_Generation++;
			}
		}
		else if (data != nullptr)
		{
			WI_SURFACE_LOG(Warning, "Surface shader {0} could not be reloaded, keeping the previous version", name);
		}

		entry.HasPending = false;
		entry.Pending = {};
		entry.PendingInline.reset();
	}

	void SurfaceShaderCache::invalidate(AssetGuid resolved)
	{
		if (const auto found = m_Indices.find(resolved); found != m_Indices.end())
		{
			start_load(*m_Entries[found->second - 1]);
		}
	}

	AssetRef SurfaceShaderCache::add_inline_shader(SurfaceShaderData shader)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_Inline[ref.resolved()] = std::make_shared<const SurfaceShaderData>(std::move(shader));
		return ref;
	}

	void SurfaceShaderCache::set_inline_shader(const AssetRef& shader, SurfaceShaderData data)
	{
		m_Inline[shader.resolved()] = std::make_shared<const SurfaceShaderData>(std::move(data));
	}

	bool SurfaceShaderCache::any_loading() const
	{
		for (const std::unique_ptr<Entry>& entry : m_Entries)
		{
			if (entry->Gpu.Loading)
			{
				return true;
			}
		}
		return false;
	}
}
