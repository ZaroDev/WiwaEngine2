#include <editor_common.h>
#include "image_utils.h"

#include <thread>

#include <renderer/ibl/environment_pack.h>

#include <algorithm>
#include <array>
#include <cmath>

// Only what's useful here: the common formats and Radiance .hdr (environment maps), no animated images.
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace wiwa
{
	namespace
	{
		// 8-bit sRGB -> linear light, as a table: it runs on every texel of every mip level.
		const std::array<float, 256>& get_srgb_to_linear_table()
		{
			static const std::array<float, 256> table = []()
			{
				std::array<float, 256> result{};
				for (int i = 0; i < 256; i++)
				{
					const float c = static_cast<float>(i) / 255.0f;
					result[i] = (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
				return result;
			}();
			return table;
		}

		u8 to_byte(float value)
		{
			return static_cast<u8>(std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
		}

		u8 linear_to_srgb_byte(float linear)
		{
			linear = std::clamp(linear, 0.0f, 1.0f);
			return to_byte((linear <= 0.0031308f) ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
		}

		// Filters the 4 texels of one 2x2 block into destination.
		template<TextureKind Kind>
		void filter_block(const u8* const (&texels)[4], u8* destination, const std::array<float, 256>& srgbToLinear)
		{
			const u32 alpha = texels[0][3] + texels[1][3] + texels[2][3] + texels[3][3];
			destination[3] = static_cast<u8>((alpha + 2) / 4);

			if constexpr (Kind == TextureKind::Color)
			{
				// Average the light, not the sRGB codes: averaging the codes darkens edges between bright and dark.
				for (int channel = 0; channel < 3; channel++)
				{
					const float sum = srgbToLinear[texels[0][channel]] + srgbToLinear[texels[1][channel]]
						+ srgbToLinear[texels[2][channel]] + srgbToLinear[texels[3][channel]];
					destination[channel] = linear_to_srgb_byte(sum * 0.25f);
				}
			}
			else if constexpr (Kind == TextureKind::Normal)
			{
				// Averaging unit vectors shortens them: renormalize, or distant bumps would look flatter than they are.
				float sum[3]{};
				for (const u8* texel : texels)
				{
					for (int channel = 0; channel < 3; channel++)
					{
						sum[channel] += static_cast<float>(texel[channel]) * (2.0f / 255.0f) - 1.0f;
					}
				}

				const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
				if (length > 1e-6f)
				{
					const float inverseLength = 1.0f / length;
					for (int channel = 0; channel < 3; channel++)
					{
						destination[channel] = to_byte(sum[channel] * inverseLength * 0.5f + 0.5f);
					}
				}
				else
				{
					destination[0] = to_byte(0.5f);
					destination[1] = to_byte(0.5f);
					destination[2] = to_byte(1.0f);
				}
			}
			else
			{
				for (int channel = 0; channel < 3; channel++)
				{
					const u32 sum = texels[0][channel] + texels[1][channel] + texels[2][channel] + texels[3][channel];
					destination[channel] = static_cast<u8>((sum + 2) / 4);
				}
			}
		}

		// Halves an image with a 2x2 box filter. Odd sizes and 1-pixel dimensions repeat the edge texel.
		template<TextureKind Kind>
		std::vector<u8> downsample(const std::vector<u8>& source, u32 sourceWidth, u32 sourceHeight)
		{
			const u32 width = std::max(1u, sourceWidth / 2);
			const u32 height = std::max(1u, sourceHeight / 2);
			std::vector<u8> result(static_cast<size_t>(width) * height * 4);

			const std::array<float, 256>& srgbToLinear = get_srgb_to_linear_table();
			const size_t sourceRowBytes = static_cast<size_t>(sourceWidth) * 4;

			for (u32 y = 0; y < height; y++)
			{
				const u8* row0 = &source[static_cast<size_t>(std::min(y * 2, sourceHeight - 1)) * sourceRowBytes];
				const u8* row1 = &source[static_cast<size_t>(std::min(y * 2 + 1, sourceHeight - 1)) * sourceRowBytes];
				u8* destination = &result[static_cast<size_t>(y) * width * 4];

				for (u32 x = 0; x < width; x++, destination += 4)
				{
					const size_t x0 = static_cast<size_t>(std::min(x * 2, sourceWidth - 1)) * 4;
					const size_t x1 = static_cast<size_t>(std::min(x * 2 + 1, sourceWidth - 1)) * 4;

					const u8* const texels[4] = { row0 + x0, row0 + x1, row1 + x0, row1 + x1 };
					filter_block<Kind>(texels, destination, srgbToLinear);
				}
			}

			return result;
		}

		std::vector<u8> downsample(const std::vector<u8>& source, u32 sourceWidth, u32 sourceHeight, TextureKind kind)
		{
			switch (kind)
			{
			case TextureKind::Color:
				return downsample<TextureKind::Color>(source, sourceWidth, sourceHeight);
			case TextureKind::Normal:
				return downsample<TextureKind::Normal>(source, sourceWidth, sourceHeight);
			default:
				return downsample<TextureKind::Data>(source, sourceWidth, sourceHeight);
			}
		}
	}

	u32 get_mip_count(u32 width, u32 height)
	{
		u32 count = 1;
		for (u32 size = std::max(width, height); size > 1; size /= 2)
		{
			count++;
		}
		return count;
	}

	std::optional<TextureData> decode_texture(std::span<const std::byte> bytes, TextureKind kind, const std::string& name, std::string& error)
	{
		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		// Always 4 channels out, whatever the file has: one GPU format covers everything.
		unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &width, &height, &channelsInFile, 4);
		if (pixels == nullptr)
		{
			const char* reason = stbi_failure_reason();
			error = "Failed to decode " + name + ": " + (reason != nullptr ? reason : "unknown error");
			return std::nullopt;
		}

		TextureData texture;
		texture.Width = static_cast<u32>(width);
		texture.Height = static_cast<u32>(height);
		texture.Kind = kind;

		const u32 mipCount = get_mip_count(texture.Width, texture.Height);
		texture.Mips.reserve(mipCount);

		const size_t byteCount = static_cast<size_t>(width) * height * 4;
		texture.Mips.emplace_back(pixels, pixels + byteCount);
		stbi_image_free(pixels);

		if (kind == TextureKind::MetallicRoughness)
		{
			// glTF only defines green (roughness) and blue (metallic). The shader reads red as ambient occlusion.
			std::vector<u8>& level0 = texture.Mips[0];
			for (size_t i = 0; i < byteCount; i += 4)
			{
				level0[i] = 255;
			}
		}

		u32 mipWidth = texture.Width;
		u32 mipHeight = texture.Height;
		for (u32 level = 1; level < mipCount; level++)
		{
			texture.Mips.push_back(downsample(texture.Mips.back(), mipWidth, mipHeight, kind));
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);
		}

		return texture;
	}

	std::optional<EquirectImage> decode_environment(std::span<const std::byte> bytes, const std::string& name, std::string& error)
	{
		constexpr int MIN_WIDTH = 16;

		const stbi_uc* data = reinterpret_cast<const stbi_uc*>(bytes.data());
		const int size = static_cast<int>(bytes.size());

		// stbi_loadf() would also accept a PNG and turn it into floats: only a real Radiance file is an environment.
		if (!stbi_is_hdr_from_memory(data, size))
		{
			error = name + ": not a Radiance .hdr file";
			return std::nullopt;
		}

		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		float* pixels = stbi_loadf_from_memory(data, size, &width, &height, &channelsInFile, 3);
		if (pixels == nullptr)
		{
			const char* reason = stbi_failure_reason();
			error = name + ": " + (reason != nullptr ? reason : "could not decode the image");
			return std::nullopt;
		}

		if (width < MIN_WIDTH)
		{
			error = name + ": only " + std::to_string(width) + " pixels wide (an environment needs at least " + std::to_string(MIN_WIDTH) + ")";
			stbi_image_free(pixels);
			return std::nullopt;
		}

		EquirectImage environment;
		environment.Width = static_cast<u32>(width);
		environment.Height = static_cast<u32>(height);
		environment.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
		stbi_image_free(pixels);
		return environment;
	}

	std::optional<EnvironmentData> cook_environment(const EquirectImage& image, std::string& error)
	{
		if (image.Width == 0 || image.Height == 0 || image.Pixels.size() != static_cast<size_t>(image.Width) * image.Height * 3)
		{
			error = "the image has no pixels";
			return std::nullopt;
		}

		const CubeMap source = equirect_to_cube(image.Pixels.data(), image.Width, image.Height, ENV_FACE_SIZE);
		const u32 threads = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
		return pack_environment(build_environment(source, threads, 8)); // the BRDF table is built by the renderer, once
	}

	TextureKind texture_kind_from_string(const std::string& name)
	{
		if (name == "data") return TextureKind::Data;
		if (name == "normal") return TextureKind::Normal;
		if (name == "metallicRoughness") return TextureKind::MetallicRoughness;
		return TextureKind::Color;
	}

	const char* texture_kind_to_string(TextureKind kind)
	{
		switch (kind)
		{
		case TextureKind::Data: return "data";
		case TextureKind::Normal: return "normal";
		case TextureKind::MetallicRoughness: return "metallicRoughness";
		default: return "color";
		}
	}
}
