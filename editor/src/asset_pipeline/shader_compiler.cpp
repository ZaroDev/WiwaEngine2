#include <editor_common.h>
#include "shader_compiler.h"
#include <core/file_io.h>

#include <set>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		namespace fs = std::filesystem;

		struct StageInfo
		{
			const char* Entry;
			const char* StageName;
			const char* D3DTarget;
			const char* DxcTarget;
		};

		StageInfo get_stage_info(ShaderStage stage)
		{
			switch (stage)
			{
			case ShaderStage::Vertex: return { "VSMain", "vertex", "vs_5_0", "vs_6_0" };
			case ShaderStage::Compute: return { "CSMain", "compute", "cs_5_0", "cs_6_0" };
			default: return { "PSMain", "pixel", "ps_5_0", "ps_6_0" };
			}
		}

		std::string read_text_file(const fs::path& path)
		{
			const std::optional<std::vector<std::byte>> bytes = read_file_bytes(path);
			if (!bytes)
			{
				return {};
			}
			return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
		}

		std::vector<u8> read_binary_file(const fs::path& path)
		{
			const std::string data = read_text_file(path);
			return std::vector<u8>(data.begin(), data.end());
		}

		// Lets #include "Common.hlsli" resolve next to the including file, then in the include directories.
		class ShaderIncludeHandler : public ID3DInclude
		{
		public:
			ShaderIncludeHandler(fs::path sourceDirectory, const std::vector<fs::path>& includeDirectories, std::set<fs::path>& included)
				: m_SourceDirectory(std::move(sourceDirectory)), m_IncludeDirectories(includeDirectories), m_Included(included) {}

			HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentData, LPCVOID* data, UINT* byteCount) override
			{
				// An include of an include is relative to that include's folder.
				fs::path parentDirectory = m_SourceDirectory;
				if (auto parent = m_Directories.find(parentData); parentData != nullptr && parent != m_Directories.end())
				{
					parentDirectory = parent->second;
				}

				std::vector<fs::path> candidates{ parentDirectory / fileName };
				for (const fs::path& directory : m_IncludeDirectories)
				{
					candidates.push_back(directory / fileName);
				}

				for (const fs::path& candidate : candidates)
				{
					std::error_code error;
					if (!fs::is_regular_file(candidate, error))
					{
						continue;
					}

					const std::string content = read_text_file(candidate);
					char* copy = new char[content.size() + 1];
					std::memcpy(copy, content.data(), content.size());
					copy[content.size()] = '\0';

					*data = copy;
					*byteCount = static_cast<UINT>(content.size());
					m_Directories[copy] = candidate.parent_path();
					m_Included.insert(fs::weakly_canonical(candidate, error));
					return S_OK;
				}

				return E_FAIL;
			}

			HRESULT __stdcall Close(LPCVOID data) override
			{
				m_Directories.erase(data);
				delete[] static_cast<const char*>(data);
				return S_OK;
			}

		private:
			fs::path m_SourceDirectory;
			const std::vector<fs::path>& m_IncludeDirectories;
			std::set<fs::path>& m_Included;
			std::unordered_map<LPCVOID, fs::path> m_Directories{};
		};

		CompiledShader compile_dxbc(const std::string& source, const fs::path& sourcePath, const StageInfo& stage,
			const ShaderCompileOptions& options, std::vector<fs::path>& includedFiles)
		{
			CompiledShader result;

			UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
			flags |= options.DebugInfo ? (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION) : D3DCOMPILE_OPTIMIZATION_LEVEL3;

			std::set<fs::path> included;
			ShaderIncludeHandler includeHandler(sourcePath.parent_path(), options.IncludeDirectories, included);

			Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
			Microsoft::WRL::ComPtr<ID3DBlob> errors;
			const std::string name = sourcePath.string();
			const HRESULT hr = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, &includeHandler,
				stage.Entry, stage.D3DTarget, flags, 0, &bytecode, &errors);

			if (errors)
			{
				result.Log.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
			}

			for (const fs::path& file : included)
			{
				if (std::find(includedFiles.begin(), includedFiles.end(), file) == includedFiles.end())
				{
					includedFiles.push_back(file);
				}
			}

			if (FAILED(hr))
			{
				return result;
			}

			// Reflection, debug and test chunks are not read at run time (the input signature stays): a release cook does not carry them.
			Microsoft::WRL::ComPtr<ID3DBlob> stripped;
			if (!options.DebugInfo
				&& SUCCEEDED(D3DStripShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
					D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS | D3DCOMPILER_STRIP_PRIVATE_DATA, &stripped)))
			{
				bytecode = stripped;
			}

			const u8* begin = static_cast<const u8*>(bytecode->GetBufferPointer());
			result.Code.assign(begin, begin + bytecode->GetBufferSize());
			result.Success = true;
			return result;
		}

		// Where to find dxc: the Vulkan SDK, then PATH.
		std::string locate_dxc()
		{
			std::error_code error;
			if (const char* sdk = std::getenv("VULKAN_SDK"))
			{
				for (const char* relative : { "Bin/dxc.exe", "bin/dxc.exe" })
				{
					const fs::path candidate = fs::path(sdk) / relative;
					if (fs::exists(candidate, error))
					{
						return candidate.string();
					}
				}
			}

			return "dxc";
		}

		const std::string& find_dxc()
		{
			static const std::string path = locate_dxc();
			return path;
		}

		// Runs a program and waits for it. Returns its exit code, or -1 if it could not be started.
		int run_process(const std::string& executable, const std::vector<std::string>& arguments)
		{
			auto quote = [](const std::string& text)
			{
				return L"\"" + fs::path(text).wstring() + L"\"";
			};

			std::wstring commandLine = quote(executable);
			for (const std::string& argument : arguments)
			{
				commandLine += L" " + quote(argument);
			}

			STARTUPINFOW startupInfo = {};
			startupInfo.cb = sizeof(startupInfo);
			PROCESS_INFORMATION processInfo = {};

			if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
			{
				return -1;
			}

			WaitForSingleObject(processInfo.hProcess, INFINITE);

			DWORD exitCode = 1;
			GetExitCodeProcess(processInfo.hProcess, &exitCode);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return static_cast<int>(exitCode);
		}

		CompiledShader compile_spirv(const fs::path& sourcePath, const StageInfo& stage, const ShaderCompileOptions& options)
		{
			CompiledShader result;

			// Every job gets its own output files so concurrent compiles never collide.
			static std::atomic<u32> jobCounter{ 0 };
			std::error_code error;
			const std::string stem = "wiwa_shader_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(jobCounter++);
			const fs::path temporaryDirectory = fs::temp_directory_path(error);
			const fs::path outputPath = temporaryDirectory / (stem + ".spv");
			const fs::path logPath = temporaryDirectory / (stem + ".log");

			std::vector<std::string> arguments =
			{
				"-spirv",
				"-T", stage.DxcTarget,
				"-E", stage.Entry,
				"-DVULKAN=1",
			};
			if (options.DebugInfo)
			{
				arguments.push_back("-Od");
			}
			for (const fs::path& directory : options.IncludeDirectories)
			{
				arguments.push_back("-I");
				arguments.push_back(directory.string());
			}
			arguments.push_back(sourcePath.string());
			arguments.push_back("-Fo");
			arguments.push_back(outputPath.string());
			arguments.push_back("-Fe"); // warnings and errors go to a file
			arguments.push_back(logPath.string());

			const int exitCode = run_process(find_dxc(), arguments);

			result.Log = read_text_file(logPath);
			result.Code = read_binary_file(outputPath);

			fs::remove(outputPath, error);
			fs::remove(logPath, error);

			if (exitCode == -1)
			{
				result.Log = "Could not start dxc. Install the Vulkan SDK, set VULKAN_SDK, or put dxc on PATH.";
				return result;
			}
			if (exitCode != 0 || result.Code.empty())
			{
				if (result.Log.empty())
				{
					result.Log = "dxc failed with exit code " + std::to_string(exitCode);
				}
				result.Code.clear();
				return result;
			}

			result.Success = true;
			return result;
		}
	}

	const char* shader_entry_point(ShaderStage stage)
	{
		return get_stage_info(stage).Entry;
	}

	const char* shader_stage_name(ShaderStage stage)
	{
		return get_stage_info(stage).StageName;
	}

	const char* shader_target_name(ShaderTarget target)
	{
		return target == ShaderTarget::Dxbc ? "DXBC" : "SPIR-V";
	}

	CompiledShader compile_shader(const std::string& source, const fs::path& sourcePath, ShaderStage stage, ShaderTarget target,
		const ShaderCompileOptions& options, std::vector<fs::path>& includedFiles)
	{
		const StageInfo info = get_stage_info(stage);
		return target == ShaderTarget::Dxbc
			? compile_dxbc(source, sourcePath, info, options, includedFiles)
			: compile_spirv(sourcePath, info, options);
	}
}
