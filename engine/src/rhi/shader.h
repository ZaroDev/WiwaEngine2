#pragma once
#include "rhi_common.h"
#include "rhi_resource.h"

#include <span>

namespace wiwa
{
	struct ShaderDescription
	{
		ShaderStage Stage = ShaderStage::NONE;
		std::span<const uint8_t> Code;
		std::string EntryPoint = "main";
		std::string DebugName = "Unkown shader";
	};

	class IShader : public IRHIResource
	{
	public:
		virtual ShaderStage get_shader_stage() const = 0;
	};

	enum class BindingType : uint8_t
	{
		UNIFORM_BUFFER, STORAGE_BUFFER, SAMPLED_TEXTURE, STORAGE_TEXTURE, SAMPLER, COMBINED_TEXTURE_SAMPLER
	};

	struct ReflectedBinding
	{
		std::string Name;
		BindingType Type;
		uint32_t Set = 0, Binding = 0;
		uint32_t ArrayCount = 0;
		ShaderStage Stages = ShaderStage::NONE;
		uint64_t BlockSize = 0;
	};

	struct ReflectedVertexInput
	{
		std::string Name;
		uint32_t Location;
		Format Format = Format::UNKNOWN;
	};

	struct ShaderReflection
	{
		std::vector<ReflectedBinding> Bindings;
		std::vector<ReflectedVertexInput> VertexInputs;
		uint64_t PushConstantSize = 0;
		uint64_t WorkerGroupSize[3] = { 1, 1, 1 }; // Compute
	};

	struct ShaderMacro
	{
		std::string Name;
		std::string Value;
	};

	enum class ShaderSourceLanguage : uint8_t
	{
		GLSL,
		HLSL
	};

	struct ShaderCompilerDescription
	{
		std::string Source;
		std::string SourceName;
		std::string EntryPoint = "main";
		ShaderStage Stage = ShaderStage::NONE;
		ShaderSourceLanguage Language = ShaderSourceLanguage::GLSL;
		std::vector<ShaderMacro> Defines;
		bool DebugInfo = false;
		bool Optimize = true;
	};

	struct ShaderCompilerResult
	{
		bool Success = false;
		std::vector<uint8_t> Code;
		ShaderReflection Reflection;
		std::string Diagnostics;
	};

	class IIncludeHandler
	{
	public:
		virtual ~IIncludeHandler() = default;
		virtual bool load(std::string_view includeName, std::string_view requestedBy, std::string& outSource) = 0;
	};

	class IShaderCompiler
	{
	public:
		virtual ~IShaderCompiler() = default;
		virtual void set_include_handler(IIncludeHandler* handler) = 0;
		virtual ShaderCompilerResult compile(const ShaderCompilerDescription& desc) = 0;
		virtual void set_cache_directory(const std::filesystem::path& path) = 0;
	};
}