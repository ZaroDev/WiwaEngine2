#pragma once

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include <nvrhi/nvrhi.h>

#if defined(RENDERER_HAS_D3D11)
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

#define SHADER_DIR "shaders"

// All shaders are written once, in HLSL ("<name>.hlsl", entry points VSMain / PSMain):
//   Vulkan: compiled to "<name>.vert.spv" / "<name>.frag.spv" by DXC at build time
//   D3D11:  compiled to DXBC (shader model 5.0) by D3DCompile when loaded

inline std::vector<char> read_binary_file(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);

	if (!file)
		throw std::runtime_error("Failed to open file: " + path.string());

	const auto size = file.tellg();

	if (size < 0)
		throw std::runtime_error("Failed to determine file size: " + path.string());

	std::vector<char> buffer(static_cast<std::size_t>(size));

	file.seekg(0, std::ios::beg);

	if (!buffer.empty() &&
		!file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
	{
		throw std::runtime_error("Failed to read file: " + path.string());
	}

	return buffer;
}

inline const char* get_shader_entry_point(nvrhi::ShaderType type)
{
	return (type == nvrhi::ShaderType::Vertex) ? "main" : "main";
}

inline nvrhi::ShaderHandle load_spirv_shader(nvrhi::IDevice* device, nvrhi::ShaderType type, const std::string& name)
{
	const char* stage = (type == nvrhi::ShaderType::Vertex) ? "vert" : "frag";
	const std::string file_name = name + "." + stage + ".spv";

	std::vector<char> spirv = read_binary_file(std::filesystem::path(SHADER_DIR) / file_name);
	if (spirv.empty())
	{
		std::cerr << "Failed to read shader " << file_name << std::endl;
		return nullptr;
	}

	// DXC keeps the HLSL entry point name in the SPIR-V module.
	nvrhi::ShaderDesc shader_description = nvrhi::ShaderDesc()
		.setShaderType(type)
		.setEntryName(get_shader_entry_point(type))
		.setDebugName(file_name);

	return device->createShader(shader_description, spirv.data(), spirv.size());
}

#if defined(RENDERER_HAS_D3D11)
// Lets #include "Common.hlsli" resolve against the shader directory.
class ShaderIncludeHandler : public ID3DInclude
{
public:
	HRESULT __stdcall Open(D3D_INCLUDE_TYPE include_type, LPCSTR file_name, LPCVOID parent_data, LPCVOID* data, UINT* byte_count) override
	{
		std::vector<char> content = read_binary_file(std::string(SHADER_DIR) + "/" + file_name);
		if (content.empty())
		{
			return E_FAIL;
		}

		char* copy = new char[content.size()];
		std::memcpy(copy, content.data(), content.size());
		*data = copy;
		*byte_count = static_cast<UINT>(content.size());
		return S_OK;
	}

	HRESULT __stdcall Close(LPCVOID data) override
	{
		delete[] static_cast<const char*>(data);
		return S_OK;
	}
};
#endif

inline nvrhi::ShaderHandle load_hlsl_shader(nvrhi::IDevice* device, nvrhi::ShaderType type, const std::string& name)
{
#if defined(RENDERER_HAS_D3D11)
	const std::string path = std::string(SHADER_DIR) + "/" + name + ".hlsl";

	std::vector<char> source = read_binary_file(path);
	if (source.empty())
	{
		std::cerr << "Failed to read shader " << path << std::endl;
		return nullptr;
	}

	const bool is_vertex = (type == nvrhi::ShaderType::Vertex);
	const char* entry_point = get_shader_entry_point(type);
	const char* target = is_vertex ? "vs_5_0" : "ps_5_0";

	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(NDEBUG)
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ShaderIncludeHandler include_handler;
	Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	HRESULT result = D3DCompile(source.data(), source.size(), path.c_str(), nullptr, &include_handler,
		entry_point, target, flags, 0, &bytecode, &errors);
	if (FAILED(result))
	{
		std::cerr << "Failed to compile " << path << " (" << entry_point << "):\n";
		if (errors)
		{
			std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << std::endl;
		}
		return nullptr;
	}

	nvrhi::ShaderDesc shader_description = nvrhi::ShaderDesc()
		.setShaderType(type)
		.setEntryName(entry_point)
		.setDebugName(name + (is_vertex ? ".vs" : ".ps"));

	return device->createShader(shader_description, bytecode->GetBufferPointer(), bytecode->GetBufferSize());
#else
	std::cerr << "This build has no D3D11 support (shader " << name << ")" << std::endl;
	return nullptr;
#endif
}

// "name" has no extension: the right path for the device's graphics API is picked here.
inline nvrhi::ShaderHandle load_shader(nvrhi::IDevice* device, nvrhi::ShaderType type, const std::string& name)
{
	if (device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
	{
		return load_hlsl_shader(device, type, name);
	}

	return load_spirv_shader(device, type, name);
}

// With zero offsets the Vulkan descriptor binding equals the NVRHI slot, which matches
// the register numbers in the HLSL (DXC maps bN / tN / sN to binding N). Slots must
// therefore be unique across resource types within one layout. D3D11 ignores this: the
// slot is the register number within each register type.
inline nvrhi::VulkanBindingOffsets make_zero_binding_offsets()
{
	nvrhi::VulkanBindingOffsets offsets;
	offsets.shaderResource = 0;
	offsets.sampler = 0;
	offsets.constantBuffer = 0;
	offsets.unorderedAccess = 0;
	return offsets;
}
