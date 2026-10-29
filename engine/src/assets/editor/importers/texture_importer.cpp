#include <pch.h>
#include "importers.h"

#if !WI_SHIPPING

#include "assets/editor/file_utils.h"
#include "assets/editor/image_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		class TextureImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "TextureImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }; }

			// kind: "color" (sRGB albedo), "data", "normal" or "metallicRoughness"
			virtual nlohmann::json default_settings() const override { return { { "kind", "color" } }; }

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				const TextureKind kind = texture_kind_from_string(ctx.settings().value("kind", std::string("color")));

				std::string error;
				std::optional<TextureData> texture = decode_texture(*bytes, kind, ctx.source().filename().string(), error);
				if (!texture)
				{
					WI_ASSET_LOG(Error, "{0}", error);
					return false;
				}

				ctx.emit(ctx.main_asset(), *texture);
				return true;
			}
		};

		class EnvironmentImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "EnvironmentImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".hdr" }; }

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				std::string error;
				std::optional<EnvironmentData> environment = decode_environment(*bytes, ctx.source().filename().string(), error);
				if (!environment)
				{
					WI_ASSET_LOG(Error, "{0}", error);
					return false;
				}

				ctx.emit(ctx.main_asset(), *environment);
				return true;
			}
		};
	}

	Scope<IImporter> create_texture_importer() { return CreateScope<TextureImporter>(); }
	Scope<IImporter> create_environment_importer() { return CreateScope<EnvironmentImporter>(); }
}

#endif
