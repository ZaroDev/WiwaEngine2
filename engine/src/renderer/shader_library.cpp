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
	}

	ShaderLibrary::ShaderLibrary(nvrhi::IDevice* device, GraphicsApi api, AssetManager* assets)
		: m_Device(device), m_Api(api), m_Assets(assets)
	{
	}

	ShaderLibrary::Entry* ShaderLibrary::find(AssetRef shader, ShaderStage stage)
	{
		const AssetGuid guid = shader.resolved();
		for (Entry& entry : m_Entries)
		{
			if (entry.Stage == stage && entry.Ref.resolved() == guid)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	const ShaderLibrary::Entry* ShaderLibrary::find(AssetRef shader, ShaderStage stage) const
	{
		return const_cast<ShaderLibrary*>(this)->find(shader, stage);
	}

	void ShaderLibrary::request(AssetRef shader, ShaderStage stage)
	{
		if (find(shader, stage) != nullptr)
		{
			return;
		}

		Entry entry;
		entry.Ref = shader;
		entry.Stage = stage;
		entry.Pending = m_Assets->load<ShaderData>(shader);
		m_Entries.push_back(std::move(entry));
	}

	nvrhi::IShader* ShaderLibrary::get(AssetRef shader, ShaderStage stage) const
	{
		const Entry* entry = find(shader, stage);
		return entry ? entry->Shader.Get() : nullptr;
	}

	bool ShaderLibrary::has_failed(AssetRef shader, ShaderStage stage) const
	{
		const Entry* entry = find(shader, stage);
		return entry != nullptr && entry->Failed;
	}

	void ShaderLibrary::invalidate(AssetGuid resolvedGuid)
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.Ref.resolved() == resolvedGuid)
			{
				entry.Pending = m_Assets->load<ShaderData>(entry.Ref);
			}
		}
	}

	void ShaderLibrary::update()
	{
		for (Entry& entry : m_Entries)
		{
			if (!entry.Pending)
			{
				continue;
			}

			switch (entry.Pending.state())
			{
			case LoadState::Loading:
				break;
			case LoadState::Failed:
				WI_SHADER_LOG(Error, "Shader {0} could not be loaded{1}", entry.Ref.Guid.to_string(), entry.Shader ? ", keeping the previous version" : "");
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
		const ShaderTarget target = (m_Api == GraphicsApi::Vulkan) ? ShaderTarget::Spirv : ShaderTarget::Dxbc;
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
