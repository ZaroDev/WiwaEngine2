#include <editor_common.h>
#include "importers.h"

#include <regex>

#include "../file_utils.h"
#include "../shader_compiler.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// The source without its // and /* */ comments, so that a commented-out entry point is not taken for a real one.
		std::string strip_comments(const std::string& source)
		{
			std::string result;
			result.reserve(source.size());

			for (size_t i = 0; i < source.size(); i++)
			{
				if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/')
				{
					while (i < source.size() && source[i] != '\n')
					{
						i++;
					}
					result += '\n';
				}
				else if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
				{
					i += 2;
					while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/'))
					{
						i++;
					}
					i++;
				}
				else
				{
					result += source[i];
				}
			}

			return result;
		}

		bool defines_entry_point(const std::string& code, const char* entry)
		{
			const std::regex pattern(std::string("\\b") + entry + "\\s*\\(");
			return std::regex_search(code, pattern);
		}

		// HLSL files become a ShaderData holding every stage they define (VSMain, PSMain, CSMain) compiled for every target
		// (DXBC and SPIR-V). Either target failing fails the import, so a machine that cannot compile one of them never
		// produces a half-working shader. Include files (.hlsli) have no entry points and no binaries.
		class ShaderImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ShaderImporter"; }
			virtual u32 version() const override { return 2; }
			virtual std::vector<std::string> extensions() const override { return { ".hlsl", ".hlsli" }; }

			// not editable in the editor: a shader's debug build is a developer switch
			virtual std::vector<ImportOption> option_schema() const override
			{
				return { ImportOption::make_bool("debugInfo", "Debug info", false, "No optimization and debug information, for a graphics debugger.") };
			}

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				const std::string source(reinterpret_cast<const char*>(bytes->data()), bytes->size());
				const std::string code = strip_comments(source);

				ShaderData shader;
				shader.Name = ctx.source().filename().string();

				ShaderCompileOptions options;
				options.DebugInfo = ctx.settings().value("debugInfo", false);
				options.IncludeDirectories.push_back(ctx.source().parent_path());
				for (const std::filesystem::path& root : ctx.get_source_roots())
				{
					options.IncludeDirectories.push_back(std::filesystem::absolute(root / "shaders"));
				}

				std::vector<std::filesystem::path> includedFiles;
				for (ShaderStage stage : { ShaderStage::Vertex, ShaderStage::Pixel, ShaderStage::Compute })
				{
					const char* entry = shader_entry_point(stage);
					if (!defines_entry_point(code, entry))
					{
						continue;
					}

					for (ShaderTarget target : { ShaderTarget::Dxbc, ShaderTarget::Spirv })
					{
						CompiledShader compiled = compile_shader(source, ctx.source(), stage, target, options, includedFiles);
						if (!compiled.Success)
						{
							WI_ASSET_LOG(Error, "{0}: {1} shader ({2}) failed to compile:\n{3}", shader.Name, shader_stage_name(stage), shader_target_name(target), compiled.Log);
							return false;
						}

						if (!compiled.Log.empty())
						{
							ctx.warn(std::string(shader_stage_name(stage)) + " (" + shader_target_name(target) + "): " + compiled.Log);
						}

						ShaderBinary binary;
						binary.Stage = stage;
						binary.Target = target;
						binary.Entry = entry;
						binary.Code = std::move(compiled.Code);
						shader.Binaries.push_back(std::move(binary));
					}
				}

				for (const std::filesystem::path& file : includedFiles)
				{
					ctx.add_source_dependency(file);
				}

				ctx.emit(ctx.main_asset(), shader);
				return true;
			}
		};
	}

	Scope<IImporter> create_shader_importer() { return CreateScope<ShaderImporter>(); }
}
