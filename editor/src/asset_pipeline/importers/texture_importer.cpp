#include <editor_common.h>
#include "importers.h"

#include "../file_utils.h"
#include "../image_utils.h"

#include <assets/asset_log.h>

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

			virtual bool editable() const override { return true; }
			virtual std::vector<ImportOption> option_schema() const override
			{
				return {
					ImportOption::make_enum("kind", "Kind", "color", { "color", "data", "normal", "metallicRoughness" },
						"color: sRGB albedo. data: linear values. normal: a tangent space normal map. metallicRoughness: glTF packed metallic (blue) and roughness (green)."),
					ImportOption::make_bool("generateMipmaps", "Generate mipmaps", true, "Off keeps only the full size level (sharp but shimmers at a distance)."),
					ImportOption::make_int("maxSize", "Max size", 0, 0, 16384, "The larger side is at most this many pixels (the image is halved until it fits). 0 means no limit."),
					ImportOption::make_bool("flipVertical", "Flip vertically", false, "Mirror the image top to bottom."),
					ImportOption::make_bool("invertGreen", "Invert green", false, "Flip the green channel: turns a DirectX (y-down) normal map into an OpenGL (y-up) one and back."),
					ImportOption::make_bool("forceOpaque", "Force opaque", false, "Set alpha to 255 everywhere (a stray alpha channel in a color map)."),
					ImportOption::make_bool("premultiplyAlpha", "Premultiply alpha", false, "Multiply color by alpha before the mipmaps are made."),
				};
			}

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				const TextureKind kind = texture_kind_from_string(ctx.settings().value("kind", std::string("color")));

				std::string error;
				TextureImportOptions options;
				options.GenerateMipmaps = ctx.settings().value("generateMipmaps", true);
				options.MaxSize = static_cast<u32>(std::max(0, ctx.settings().value("maxSize", 0)));
				options.FlipVertical = ctx.settings().value("flipVertical", false);
				options.InvertGreen = ctx.settings().value("invertGreen", false);
				options.ForceOpaque = ctx.settings().value("forceOpaque", false);
				options.PremultiplyAlpha = ctx.settings().value("premultiplyAlpha", false);
				std::optional<TextureData> texture = decode_texture(*bytes, kind, ctx.source().filename().string(), error, options);
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
			virtual u32 version() const override { return 2; } // 2: cooked into a cube with prefiltered mips and SH
			virtual std::vector<std::string> extensions() const override { return { ".hdr" }; }

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				std::string error;
				std::optional<EquirectImage> image = decode_environment(*bytes, ctx.source().filename().string(), error);
				std::optional<EnvironmentData> environment = image ? cook_environment(*image, error) : std::nullopt;
				if (!environment)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", ctx.source().filename().string(), error);
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
