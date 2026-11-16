#include <pch.h>
#include "shader_library.h"

#define WI_SHADER_LOG(level, ...) WI_LOG(level, "Shaders", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		nvrhi::ShaderType to_nvrhi(ShaderStage stage)
		{
			switch (stage)
			{
			case ShaderStage::Vertex: return nvrhi::ShaderType::Vertex;
			case ShaderStage::Compute: return nvrhi::ShaderType::Compute;
			default: return nvrhi::ShaderType::Pixel;
			}
		}

		const char* stage_name(ShaderStage stage)
		{
			switch (stage)
			{
			case ShaderStage::Vertex: return "vertex";
			case ShaderStage::Compute: return "compute";
			default: return "pixel";
			}
		}

		// A path that is still not in the catalog after this long is probably a typo: say so once.
		constexpr auto UNKNOWN_PATH_WARNING_DELAY = std::chrono::seconds(10);
	}

	ShaderLibrary::ShaderLibrary(nvrhi::IDevice* device, GraphicsApi api, AssetManager* assets)
		: m_Device(device), m_Api(api), m_Assets(assets)
	{
	}

	ShaderLibrary::Entry* ShaderLibrary::find(const std::string& path, ShaderStage stage)
	{
		const std::string normalized = AssetCatalog::normalize(path);
		for (Entry& entry : m_Entries)
		{
			if (entry.Stage == stage && entry.Path == normalized)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	const ShaderLibrary::Entry* ShaderLibrary::find(const std::string& path, ShaderStage stage) const
	{
		return const_cast<ShaderLibrary*>(this)->find(path, stage);
	}

	void ShaderLibrary::request(const std::string& path, ShaderStage stage)
	{
		if (find(path, stage) != nullptr)
		{
			return;
		}

		Entry entry;
		entry.Path = AssetCatalog::normalize(path);
		entry.Stage = stage;
		entry.Requested = std::chrono::steady_clock::now();
		m_Entries.push_back(std::move(entry)); // resolved and loaded by update()
	}

	nvrhi::IShader* ShaderLibrary::get(const std::string& path, ShaderStage stage) const
	{
		const Entry* entry = find(path, stage);
		return entry ? entry->Shader.Get() : nullptr;
	}

	bool ShaderLibrary::has_failed(const std::string& path, ShaderStage stage) const
	{
		const Entry* entry = find(path, stage);
		return entry != nullptr && entry->Failed;
	}

	void ShaderLibrary::invalidate(AssetGuid resolvedGuid)
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.Ref.valid() && entry.Ref.resolved() == resolvedGuid)
			{
				entry.Pending = m_Assets->load<ShaderData>(entry.Ref);
			}
		}
	}

	void ShaderLibrary::update()
	{
		for (Entry& entry : m_Entries)
		{
			if (!entry.Ref.valid())
			{
				if (std::optional<AssetRef> ref = m_Assets->find_asset(entry.Path))
				{
					entry.Ref = *ref;
					entry.Pending = m_Assets->load<ShaderData>(entry.Ref);
				}
				else if (!entry.WarnedUnknown && std::chrono::steady_clock::now() - entry.Requested > UNKNOWN_PATH_WARNING_DELAY)
				{
					entry.WarnedUnknown = true;
					WI_SHADER_LOG(Error, "{0} is not in the asset catalog (is the path right, and was the shader imported?)", entry.Path);
				}
			}

			if (!entry.Pending)
			{
				continue;
			}

			switch (entry.Pending.state())
			{
			case LoadState::Loading:
				break;
			case LoadState::Failed:
				WI_SHADER_LOG(Error, "Shader {0} could not be loaded{1}", entry.Path, entry.Shader ? ", keeping the previous version" : "");
				entry.Failed = entry.Shader == nullptr;
				entry.Pending = {};
				break;
			case LoadState::Ready:
				finish(entry);
				break;
			}
		}
	}

	void ShaderLibrary::finish(Entry& entry)
	{
		const ShaderData* data = entry.Pending.get();
		const ShaderTarget target = shader_target_for(m_Api);
		const ShaderBinary* binary = data->find(entry.Stage, target);

		nvrhi::ShaderHandle shader;
		if (binary == nullptr)
		{
			WI_SHADER_LOG(Error, "{0} has no {1} {2} binary (was it cooked for this backend?)", data->Name, stage_name(entry.Stage), target == ShaderTarget::Spirv ? "SPIR-V" : "DXBC");
		}
		else
		{
			const nvrhi::ShaderDesc description = nvrhi::ShaderDesc()
				.setShaderType(to_nvrhi(entry.Stage))
				.setEntryName(binary->Entry.c_str())
				.setDebugName(data->Name);
			shader = m_Device->createShader(description, binary->Code.data(), binary->Code.size());
			if (shader == nullptr)
			{
				WI_SHADER_LOG(Error, "{0}: the device rejected the {1} shader", data->Name, stage_name(entry.Stage));
			}
		}

		if (shader != nullptr)
		{
			entry.Shader = std::move(shader);
			entry.Failed = false;
			m_Generation++;
			WI_SHADER_LOG(Info, "{0} ({1}) ready", data->Name, stage_name(entry.Stage));
		}
		else if (entry.Shader == nullptr)
		{
			entry.Failed = true; // a reload that fails keeps the version in use
		}

		entry.Pending = {};
	}

	bool ShaderLibrary::all_ready() const
	{
		return std::all_of(m_Entries.begin(), m_Entries.end(), [](const Entry& entry) { return entry.Shader != nullptr; });
	}

	bool ShaderLibrary::any_failed() const
	{
		return std::any_of(m_Entries.begin(), m_Entries.end(), [](const Entry& entry) { return entry.Failed; });
	}

	const char* ShaderLibrary::get_status() const
	{
		if (any_failed())
		{
			return "failed";
		}
		return all_ready() ? "ready" : "loading";
	}
}
