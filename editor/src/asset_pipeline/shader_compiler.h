#pragma once

#include <assets/asset_data.h>

namespace wiwa
{
	struct ShaderCompileOptions
	{
		// Where #include "X" is looked for after the folder of the including file.
		std::vector<std::filesystem::path> IncludeDirectories{};
		// Debug information and no optimization, for stepping through a shader in a graphics debugger.
		bool DebugInfo = false;
	};

	struct CompiledShader
	{
		bool Success = false;
		std::vector<u8> Code{};
		std::string Log{}; // the compiler's warnings and errors
	};

	// The entry point name of a stage: VSMain, PSMain or CSMain.
	const char* shader_entry_point(ShaderStage stage);
	const char* shader_stage_name(ShaderStage stage);
	const char* shader_target_name(ShaderTarget target);

	// Compiles one stage of an HLSL file for one target:
	//   Dxbc:  D3DCompile, shader model 5.0 (D3D11 and D3D12 take the same binary)
	//   Spirv: dxc -spirv with VULKAN defined, run as a child process (the Vulkan SDK's, found through VULKAN_SDK or PATH)
	// includedFiles receives every file the compile read through #include (Dxbc only), so the caller can watch them.
	// Thread-safe.
	CompiledShader compile_shader(const std::string& source, const std::filesystem::path& sourcePath, ShaderStage stage, ShaderTarget target,
		const ShaderCompileOptions& options, std::vector<std::filesystem::path>& includedFiles);
}
