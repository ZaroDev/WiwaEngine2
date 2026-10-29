#pragma once

#include <optional>
#include <span>
#include <string>

#include "assets/asset_data.h"

namespace wiwa
{
	// Number of mip levels of a full chain down to 1x1.
	u32 get_mip_count(u32 width, u32 height);

	// Decodes PNG/JPEG/TGA/BMP bytes to RGBA8 and builds the whole mip chain (averaged the right way for kind).
	// name is only used in error messages.
	std::optional<TextureData> decode_texture(std::span<const std::byte> bytes, TextureKind kind, const std::string& name, std::string& error);

	// Decodes a Radiance .hdr file (an equirectangular environment, at least 16 pixels wide).
	std::optional<EnvironmentData> decode_environment(std::span<const std::byte> bytes, const std::string& name, std::string& error);

	TextureKind texture_kind_from_string(const std::string& name);
	const char* texture_kind_to_string(TextureKind kind);
}
