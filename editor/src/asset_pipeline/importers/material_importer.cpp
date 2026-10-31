#include <editor_common.h>
#include "importers.h"

#include "../material_file.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// .wmat: a material, cooked into a MaterialData. The texture paths are imported as dependencies (a path is relative to
		// the .wmat, or to the working directory like "Assets/..."; a guid names an asset directly). A texture that cannot be
		// found is a warning and an invalid reference, so the material still draws. A file that cannot be parsed fails the
		// import, which keeps the previously cooked material.
		class MaterialImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "MaterialImporter"; }
			virtual u32 version() const override { return 2; }
			virtual std::vector<std::string> extensions() const override { return { ".wmat" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				MaterialFileData data;
				std::string error;
				if (!read_material_file(ctx.source(), data, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				MaterialData material = data.Material;
				material.BaseColorTexture = resolve_texture(ctx, data.BaseColorPath, "color", "baseColor");
				material.NormalTexture = resolve_texture(ctx, data.NormalPath, "normal", "normal");
				material.OrmTexture = resolve_texture(ctx, data.OrmPath, "metallicRoughness", "orm");
				material.EmissiveTexture = resolve_texture(ctx, data.EmissivePath, "color", "emissive");

				std::vector<AssetRef> dependencies = { material.BaseColorTexture, material.NormalTexture, material.OrmTexture, material.EmissiveTexture };
				material.Shader = resolve_shader(ctx, data.ShaderPath);
				if (material.Shader.valid())
				{
					dependencies.push_back(material.Shader);
					pack_params(ctx, data, material);
					dependencies.insert(dependencies.end(), std::begin(material.ParamTextures), std::end(material.ParamTextures));
				}

				ctx.emit(ctx.main_asset(), material, dependencies);
				return true;
			}

		private:
			// The shader as a dependency, imported (and so cooked) first. A shader that fails to compile still has its guid, so the
			// material keeps the reference; one that cannot be found is a warning and no reference.
			static AssetRef resolve_shader(ImportContext& ctx, const std::string& text)
			{
				if (text.empty())
				{
					return {};
				}

				std::filesystem::path path;
				std::error_code error;
				if (std::optional<AssetGuid> guid = AssetGuid::from_string(text))
				{
					std::optional<std::filesystem::path> known = ctx.path_of(*guid);
					if (!known)
					{
						ctx.warn("shader " + text + " is not an asset in the project");
						return {};
					}
					path = *known;
				}
				else
				{
					path = ctx.source().parent_path() / text;
					if (!std::filesystem::exists(path, error))
					{
						path = std::filesystem::absolute(text);
					}
					if (!std::filesystem::exists(path, error))
					{
						ctx.warn("shader " + text + " does not exist");
						return {};
					}
				}

				std::optional<AssetRef> ref = ctx.import_dependency(path);
				if (!ref)
				{
					ctx.warn("shader " + text + " cannot be imported");
					return {};
				}

				// the material is cooked again when the shader file changes (its parameter table is packed into the material)
				ctx.add_source_dependency(std::filesystem::absolute(path));
				return *ref;
			}

			static void put(std::vector<u8>& block, u32 offset, const void* data, size_t size)
			{
				if (static_cast<size_t>(offset) + size <= block.size())
				{
					std::memcpy(block.data() + offset, data, size);
				}
			}

			// The values of the file packed into the block of the shader (defaults for what the file leaves out), and the texture
			// parameters resolved into ParamTextures. Without a cooked shader the block stays empty.
			static void pack_params(ImportContext& ctx, const MaterialFileData& data, MaterialData& material)
			{
				const std::optional<SurfaceShaderData> shader = ctx.load_cooked<SurfaceShaderData>(material.Shader);
				if (!shader)
				{
					ctx.warn("shader " + data.ShaderPath + " is not cooked (it failed to import?); the material has no parameters");
					return;
				}

				material.ParamBytes.assign(shader->BlockSize, 0);

				for (size_t i = 0; i < shader->Params.size(); i++)
				{
					const SurfaceParam& param = shader->Params[i];
					const auto found = data.Params.find(param.Name);
					const u32 count = param.Type == SurfaceParamType::Float2 ? 2 : param.Type == SurfaceParamType::Float3 ? 3 : param.Type == SurfaceParamType::Float4 ? 4 : 1;

					float values[4] = { param.Default[0], param.Default[1], param.Default[2], param.Default[3] };
					bool bad = false;
					if (found != data.Params.end())
					{
						bad = !read_value(param, count, found->second, values);
						if (bad)
						{
							ctx.warn("parameter " + param.Name + " has a value of the wrong type; the default is used");
							std::copy(std::begin(param.Default), std::end(param.Default), values);
						}
					}

					const bool ranged = param.Min < param.Max;
					for (u32 k = 0; k < count; k++)
					{
						if (ranged && param.Type != SurfaceParamType::Bool)
						{
							values[k] = std::clamp(values[k], param.Min, param.Max);
						}
					}

					switch (param.Type)
					{
					case SurfaceParamType::Int:
					{
						const i32 v = static_cast<i32>(std::clamp(values[0], -2147483520.0f, 2147483520.0f));
						put(material.ParamBytes, param.Offset, &v, 4);
						break;
					}
					case SurfaceParamType::Bool:
					{
						const u32 v = values[0] != 0.0f ? 1u : 0u;
						put(material.ParamBytes, param.Offset, &v, 4);
						break;
					}
					default:
						put(material.ParamBytes, param.Offset, values, count * 4);
						break;
					}
				}

				for (size_t t = 0; t < shader->Textures.size() && t < SURFACE_MAX_TEXTURES; t++)
				{
					const auto found = data.Params.find(shader->Textures[t]);
					if (found == data.Params.end())
					{
						continue;
					}
					if (!found->second.is_string())
					{
						ctx.warn("parameter " + shader->Textures[t] + " must be a texture path or guid; it is ignored");
						continue;
					}
					material.ParamTextures[t] = resolve_texture(ctx, found->second.get<std::string>(), "color", shader->Textures[t].c_str());
				}

				for (const auto& [name, value] : data.Params)
				{
					const bool known = std::any_of(shader->Params.begin(), shader->Params.end(), [&](const SurfaceParam& p) { return p.Name == name; }) ||
						std::find(shader->Textures.begin(), shader->Textures.end(), name) != shader->Textures.end();
					if (!known)
					{
						ctx.warn("parameter " + name + " is not a parameter of the shader; it is ignored");
					}
				}
			}

			// A JSON value as the floats of a parameter. False if it has the wrong type (a number for an array, a fractional int, ...).
			static bool read_value(const SurfaceParam& param, u32 count, const nlohmann::json& value, float (&out)[4])
			{
				if (param.Type == SurfaceParamType::Bool)
				{
					if (!value.is_boolean())
					{
						return false;
					}
					out[0] = value.get<bool>() ? 1.0f : 0.0f;
					return true;
				}

				if (count == 1)
				{
					if (!value.is_number())
					{
						return false;
					}
					const double number = value.get<double>();
					if (param.Type == SurfaceParamType::Int && number != std::floor(number))
					{
						return false;
					}
					out[0] = static_cast<float>(number);
					return true;
				}

				if (!value.is_array() || value.size() != count)
				{
					return false;
				}
				for (u32 k = 0; k < count; k++)
				{
					if (!value[k].is_number())
					{
						return false;
					}
					out[k] = value[k].get<float>();
				}
				return true;
			}

			static AssetRef resolve_texture(ImportContext& ctx, const std::string& text, const char* kind, const char* slot)
			{
				if (text.empty())
				{
					return {};
				}

				if (std::optional<AssetGuid> guid = AssetGuid::from_string(text))
				{
					if (ctx.asset_exists(AssetRef{ *guid, 0 }))
					{
						return AssetRef{ *guid, 0 };
					}
					ctx.warn(std::string(slot) + " texture " + text + " is not an asset in the project");
					return {};
				}

				std::filesystem::path path = ctx.source().parent_path() / text;
				std::error_code error;
				if (!std::filesystem::exists(path, error))
				{
					path = std::filesystem::absolute(text);
				}
				if (!std::filesystem::exists(path, error))
				{
					ctx.warn(std::string(slot) + " texture " + text + " does not exist");
					return {};
				}

				const nlohmann::json settings = { { "kind", kind } };
				if (std::optional<AssetRef> ref = ctx.import_dependency(path, &settings))
				{
					return *ref;
				}
				ctx.warn(std::string(slot) + " texture " + text + " could not be imported");
				return {};
			}
		};
	}

	Scope<IImporter> create_material_importer() { return CreateScope<MaterialImporter>(); }
}
