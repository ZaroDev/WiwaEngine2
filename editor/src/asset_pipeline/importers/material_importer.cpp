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
			virtual u32 version() const override { return 1; }
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

				ctx.emit(ctx.main_asset(), material,
					{ material.BaseColorTexture, material.NormalTexture, material.OrmTexture, material.EmissiveTexture });
				return true;
			}

		private:
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
