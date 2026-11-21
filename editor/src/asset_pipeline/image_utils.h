#pragma once

#include <optional>
#include <span>
#include <string>

#include "assets/asset_data.h"

namespace wiwa
{
	// Number of mip levels of a full chain down to 1x1.
	u32 get_mip_count(u32 width, u32 height);

	// What the import settings of a texture ask of the decoder.
	struct TextureImportOptions
	{
		bool GenerateMipmaps = true; // false: only the first level is kept
		u32 MaxSize = 0;             // 0: no limit; else the top levels are dropped until the larger side is at most this
		bool FlipVertical = false;   // mirror the image top to bottom
		bool InvertGreen = false;    // flip the green channel (a normal map from the other convention: DirectX y-down against OpenGL y-up)
		bool ForceOpaque = false;    // alpha 255 everywhere
		bool PremultiplyAlpha = false; // rgb multiplied by alpha (done on the stored values, before the mips)
		bool CompressBc7 = true;     // BC7 (a quarter of the memory) when the size is a multiple of 4; false keeps RGBA8
	};

	// Replaces the RGBA8 levels of the texture with BC7 blocks. Returns false (the texture is left as it was) when it is not RGBA8 or its
	// size is not a multiple of 4.
	bool compress_texture_bc7(TextureData& texture);

	// Decodes PNG/JPEG/TGA/BMP bytes to RGBA8 and builds the whole mip chain (averaged the right way for kind).
	// name is only used in error messages.
	std::optional<TextureData> decode_texture(std::span<const std::byte> bytes, TextureKind kind, const std::string& name, std::string& error, const TextureImportOptions& options = {});

	// A decoded Radiance .hdr file: linear float RGB, equirectangular.
	struct EquirectImage
	{
		u32 Width = 0;
		u32 Height = 0;
		std::vector<float> Pixels{}; // Width * Height * 3
	};

	// Decodes a Radiance .hdr file (an equirectangular environment, at least 16 pixels wide).
	std::optional<EquirectImage> decode_environment(std::span<const std::byte> bytes, const std::string& name, std::string& error);

	// Cooks the image into what the renderer uses: a cube of ENV_FACE_SIZE with prefiltered mips and the SH (see ibl_math.h).
	std::optional<EnvironmentData> cook_environment(const EquirectImage& image, std::string& error);

	TextureKind texture_kind_from_string(const std::string& name);
	const char* texture_kind_to_string(TextureKind kind);
}
