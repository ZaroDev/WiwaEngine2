#include <editor_common.h>
#include "importers.h"

#include <future>
#include <regex>
#include <set>

#include "../file_utils.h"
#include "../shader_compiler.h"
#include "../surface_shader_parser.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		namespace fs = std::filesystem;

		// Each pass variant is the user file composed into an entry template of the engine (engine/shaders/surface_*.hlsl).
		struct VariantSpec
		{
			SurfaceVariantKind Kind;
			const char* Template;
			bool Masked;
			const char* Name;
		};

		constexpr VariantSpec VARIANTS[SURFACE_VARIANT_COUNT] =
		{
			{ SurfaceVariantKind::Forward, "surface_forward.hlsl", false, "forward" },
			{ SurfaceVariantKind::Depth, "surface_depth.hlsl", false, "depth" },
			{ SurfaceVariantKind::DepthMasked, "surface_depth.hlsl", true, "depth masked" },
			{ SurfaceVariantKind::Velocity, "surface_velocity.hlsl", false, "velocity" },
			{ SurfaceVariantKind::Shadow, "surface_shadow.hlsl", false, "shadow" },
			{ SurfaceVariantKind::ShadowMasked, "surface_shadow.hlsl", true, "shadow masked" },
		};

		// The first register of the @param textures (t16..t19, set 3 on Vulkan; see surface_contract.hlsli).
		constexpr u32 FIRST_TEXTURE_REGISTER = 16;

		// How the generated LoadMaterialParams reads a parameter from the block in uSurfaceParams.
		std::string load_expression(const SurfaceParam& param)
		{
			const std::string at = "base + " + std::to_string(param.Offset) + "u";
			switch (param.Type)
			{
			case SurfaceParamType::Float2: return "asfloat(uSurfaceParams.Load2(" + at + "))";
			case SurfaceParamType::Float3: return "asfloat(uSurfaceParams.Load3(" + at + "))";
			case SurfaceParamType::Float4: return "asfloat(uSurfaceParams.Load4(" + at + "))";
			case SurfaceParamType::Int: return "asint(uSurfaceParams.Load(" + at + "))";
			case SurfaceParamType::Bool: return "(uSurfaceParams.Load(" + at + ") != 0u)";
			default: return "asfloat(uSurfaceParams.Load(" + at + "))";
			}
		}

		std::string generate_loader(const ParsedSurface& parsed)
		{
			std::string code = "MaterialParams LoadMaterialParams(uint materialSlot)\n{\n\tMaterialParams p = (MaterialParams)0;\n";
			if (!parsed.Params.empty())
			{
				code += "\tuint base = SurfaceParamOffset(materialSlot);\n";
				for (const SurfaceParam& param : parsed.Params)
				{
					code += "\tp." + param.Name + " = " + load_expression(param) + ";\n";
				}
			}
			code += "\treturn p;\n}\n";
			return code;
		}

		// FNV-1a over what the generated code depends on: the block layout and the texture slots (defaults, ranges and labels
		// are not part of it).
		u64 layout_hash(const ParsedSurface& parsed)
		{
			std::string text = "surface-layout-1;" + std::to_string(parsed.BlockSize) + ";";
			for (const SurfaceParam& param : parsed.Params)
			{
				text += param.Name + ":" + std::to_string(static_cast<u32>(param.Type)) + ":" + std::to_string(param.Offset) + ";";
			}
			for (const std::string& texture : parsed.Textures)
			{
				text += "t:" + texture + ";";
			}

			u64 hash = 0xcbf29ce484222325ull;
			for (const char c : text)
			{
				hash ^= static_cast<u8>(c);
				hash *= 0x100000001b3ull;
			}
			return hash == 0 ? 1 : hash;
		}

		// The user file with its @param textures bound to their registers. Only those declaration lines change, so the line
		// numbers of compiler errors stay those of the file.
		bool bind_textures(const std::string& source, const ParsedSurface& parsed, std::string& bound, std::string& error)
		{
			std::vector<std::string> lines;
			size_t start = 0;
			while (start <= source.size())
			{
				const size_t end = source.find('\n', start);
				if (end == std::string::npos)
				{
					lines.push_back(source.substr(start));
					break;
				}
				lines.push_back(source.substr(start, end - start + 1));
				start = end + 1;
			}

			for (size_t k = 0; k < parsed.Textures.size(); k++)
			{
				const u32 line = parsed.TextureLines[k];
				const std::string& name = parsed.Textures[k];
				const std::regex declaration("\\bTexture2D\\s+" + name + "\\s*;");
				std::smatch match;
				if (line == 0 || line > lines.size() || !std::regex_search(lines[line - 1], match, declaration))
				{
					error = std::to_string(line) + ": cannot bind the texture '" + name + "'";
					return false;
				}
				const std::string slot = std::to_string(FIRST_TEXTURE_REGISTER + k);
				lines[line - 1] = match.prefix().str() + "VK_BINDING(" + slot + ", 3) Texture2D " + name + " : register(t" + slot + ");" + match.suffix().str();
			}

			bound.clear();
			for (const std::string& text : lines)
			{
				bound += text;
			}
			return true;
		}

		// generated header + user file + generated parameter loader + the entry template
		std::string compose(const VariantSpec& variant, const ParsedSurface& parsed, const std::string& userSource, const fs::path& userPath, const std::string& loader)
		{
			std::string source = "#define SURFACE_COMPOSED 1\n";
			if (variant.Masked)
			{
				source += "#define SURFACE_MASKED 1\n";
			}
			if (parsed.HasVertexOffset)
			{
				source += "#define SURFACE_HAS_VERTEX_OFFSET 1\n";
			}
			source += "#include \"surface_contract.hlsli\"\n";
			if (parsed.ParamsStructSource.empty())
			{
				source += "struct MaterialParams\n{\n\tint SurfaceUnused;\n};\n";
			}
			source += "#line 1 \"" + userPath.generic_string() + "\"\n";
			source += userSource;
			source += "\n#line 1 \"" + userPath.filename().generic_string() + " (generated " + variant.Name + ")\"\n";
			source += loader;
			source += "#define SURFACE_HOOKS_READY 1\n";
			source += "#include \"" + std::string(variant.Template) + "\"\n";
			return source;
		}

		// "<line>: <message>" as compilers print it: "file(line): message"
		std::string located(const std::string& file, const std::string& error)
		{
			const size_t colon = error.find(": ");
			return colon == std::string::npos ? file + ": " + error : file + "(" + error.substr(0, colon) + "): " + error.substr(colon + 2);
		}

		struct CompileJob
		{
			SurfaceVariantKind Kind = SurfaceVariantKind::Forward;
			const char* VariantName = "";
			ShaderStage Stage = ShaderStage::Vertex;
			ShaderTarget Target = ShaderTarget::Dxbc;
			u32 VariantIndex = 0;
			std::future<CompiledShader> Result{};
			std::vector<fs::path> Included{};
		};

		// .wshader: a user surface shader (see engine/shaders/surface_contract.hlsli), cooked into a SurfaceShaderData holding its
		// parameter table and every pass variant for both stages and both targets. A parse error or any compile failure fails the
		// import, which keeps the previously cooked version.
		class SurfaceShaderImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "SurfaceShaderImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wshader" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}
				const std::string source(reinterpret_cast<const char*>(bytes->data()), bytes->size());

				ParsedSurface parsed;
				std::string error;
				if (!parse_surface_shader(source, parsed, error))
				{
					WI_ASSET_LOG(Error, "{0}", located(file, error));
					return false;
				}

				std::string bound;
				if (!bind_textures(source, parsed, bound, error))
				{
					WI_ASSET_LOG(Error, "{0}", located(file, error));
					return false;
				}

				ShaderCompileOptions options;
				options.IncludeDirectories.push_back(fs::absolute(ctx.source().parent_path()));
				for (const fs::path& root : ctx.get_source_roots())
				{
					options.IncludeDirectories.push_back(fs::absolute(root / "shaders"));
				}

				// SPIR-V is compiled from a file, so every composed variant is written to a folder of its own in the temp directory.
				static std::atomic<u32> counter{ 0 };
				std::error_code fsError;
				const fs::path workDirectory = fs::temp_directory_path(fsError) / "wiwa_surface_shaders";
				fs::create_directories(workDirectory, fsError);
				const std::string stem = ctx.main_asset().Guid.to_string() + "_" + std::to_string(counter++);

				const std::string loader = generate_loader(parsed);
				const fs::path userPath = fs::absolute(ctx.source()).lexically_normal();

				std::vector<fs::path> composedFiles;
				std::vector<std::string> composedSources;
				for (u32 v = 0; v < SURFACE_VARIANT_COUNT; v++)
				{
					const fs::path path = workDirectory / (stem + "_" + std::to_string(v) + ".hlsl");
					std::string composed = compose(VARIANTS[v], parsed, bound, userPath, loader);
					{
						std::ofstream out(path, std::ios::binary | std::ios::trunc);
						out << composed;
						if (!out)
						{
							WI_ASSET_LOG(Error, "{0}: cannot write {1}", file, path.string());
							return false;
						}
					}
					composedFiles.push_back(path);
					composedSources.push_back(std::move(composed));
				}

				// The 24 compiles are independent; dxc runs as a child process, so they run side by side.
				std::vector<CompileJob> jobs;
				jobs.reserve(SURFACE_VARIANT_COUNT * 4);
				for (u32 v = 0; v < SURFACE_VARIANT_COUNT; v++)
				{
					for (ShaderStage stage : { ShaderStage::Vertex, ShaderStage::Pixel })
					{
						for (ShaderTarget target : { ShaderTarget::Dxbc, ShaderTarget::Spirv })
						{
							CompileJob& job = jobs.emplace_back();
							job.Kind = VARIANTS[v].Kind;
							job.VariantIndex = v;
							job.VariantName = VARIANTS[v].Name;
							job.Stage = stage;
							job.Target = target;
						}
					}
				}
				for (size_t j = 0; j < jobs.size(); j++)
				{
					CompileJob* job = &jobs[j];
					const size_t v = job->VariantIndex;
					job->Result = std::async(std::launch::async, [&composedSources, &composedFiles, &options, job, v]()
						{
							return compile_shader(composedSources[v], composedFiles[v], job->Stage, job->Target, options, job->Included);
						});
				}

				SurfaceShaderData shader;
				shader.Name = file;
				shader.Params = parsed.Params;
				shader.BlockSize = parsed.BlockSize;
				shader.Textures = parsed.Textures;
				shader.HasVertexOffset = parsed.HasVertexOffset;
				shader.LayoutHash = layout_hash(parsed);

				bool failed = false;
				std::set<std::string> warnings;
				std::set<fs::path> included;
				for (CompileJob& job : jobs)
				{
					CompiledShader compiled = job.Result.get();
					included.insert(job.Included.begin(), job.Included.end());
					if (failed)
					{
						continue;
					}
					if (!compiled.Success)
					{
						// the first failure in a fixed order, so the same error is reported every time
						WI_ASSET_LOG(Error, "{0}: {1} {2} shader ({3}) failed to compile:\n{4}", file, job.VariantName, shader_stage_name(job.Stage),
							shader_target_name(job.Target), compiled.Log);
						failed = true;
						continue;
					}
					if (!compiled.Log.empty())
					{
						warnings.insert(std::string(shader_target_name(job.Target)) + ": " + compiled.Log);
					}

					SurfaceVariant variant;
					variant.Kind = job.Kind;
					variant.Stage = job.Stage;
					variant.Target = job.Target;
					variant.Code = std::move(compiled.Code);
					shader.Variants.push_back(std::move(variant));
				}

				for (const fs::path& path : composedFiles)
				{
					fs::remove(path, fsError);
				}

				if (failed)
				{
					return false;
				}

				for (const std::string& warning : warnings)
				{
					ctx.warn(warning);
				}
				for (const fs::path& path : included)
				{
					ctx.add_source_dependency(path);
				}

				ctx.emit(ctx.main_asset(), shader);
				return true;
			}
		};
	}

	Scope<IImporter> create_surface_shader_importer() { return CreateScope<SurfaceShaderImporter>(); }
}
