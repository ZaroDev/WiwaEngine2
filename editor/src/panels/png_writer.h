#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

// A minimal PNG writer for RGBA8 images: no compression (stored deflate blocks), which is plenty for a thumbnail or a screenshot of a preview.
namespace png_writer
{
	inline uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
	{
		static const std::array<uint32_t, 256> table = []
		{
			std::array<uint32_t, 256> result{};
			for (uint32_t i = 0; i < 256; i++)
			{
				uint32_t value = i;
				for (int bit = 0; bit < 8; bit++)
				{
					value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
				}
				result[i] = value;
			}
			return result;
		}();
		crc = ~crc;
		for (size_t i = 0; i < size; i++)
		{
			crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
		}
		return ~crc;
	}

	inline void put_u32(std::vector<uint8_t>& out, uint32_t value)
	{
		out.push_back(static_cast<uint8_t>(value >> 24));
		out.push_back(static_cast<uint8_t>(value >> 16));
		out.push_back(static_cast<uint8_t>(value >> 8));
		out.push_back(static_cast<uint8_t>(value));
	}

	inline void put_chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
	{
		put_u32(out, static_cast<uint32_t>(data.size()));
		std::vector<uint8_t> typed(type, type + 4);
		typed.insert(typed.end(), data.begin(), data.end());
		out.insert(out.end(), typed.begin(), typed.end());
		put_u32(out, crc32(typed.data(), typed.size()));
	}

	// Writes width x height RGBA8 pixels (row 0 on top) to path. False when the file cannot be written or the size does not fit the pixels.
	inline bool write_rgba(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba)
	{
		if (width == 0 || height == 0 || rgba.size() != static_cast<size_t>(width) * height * 4)
		{
			return false;
		}

		// Every row is preceded by the filter byte 0 (none).
		std::vector<uint8_t> raw;
		raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
		for (uint32_t y = 0; y < height; y++)
		{
			raw.push_back(0);
			raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y) * width * 4, rgba.begin() + static_cast<std::ptrdiff_t>(y + 1) * width * 4);
		}

		// zlib: header, stored blocks of at most 65535 bytes, Adler-32.
		std::vector<uint8_t> zlib = { 0x78, 0x01 };
		size_t offset = 0;
		do
		{
			const size_t length = std::min<size_t>(65535, raw.size() - offset);
			const bool last = offset + length >= raw.size();
			zlib.push_back(last ? 1 : 0);
			zlib.push_back(static_cast<uint8_t>(length & 0xFF));
			zlib.push_back(static_cast<uint8_t>(length >> 8));
			zlib.push_back(static_cast<uint8_t>(~length & 0xFF));
			zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xFF));
			zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
			offset += length;
		} while (offset < raw.size());
		uint32_t a = 1;
		uint32_t b = 0;
		for (uint8_t byte : raw)
		{
			a = (a + byte) % 65521u;
			b = (b + a) % 65521u;
		}
		put_u32(zlib, (b << 16) | a);

		std::vector<uint8_t> file = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		std::vector<uint8_t> header;
		put_u32(header, width);
		put_u32(header, height);
		header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8 bits, RGBA, deflate, no filter, no interlace
		put_chunk(file, "IHDR", header);
		put_chunk(file, "IDAT", zlib);
		put_chunk(file, "IEND", {});

		std::error_code error;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), error);
		}
		std::ofstream stream(path, std::ios::binary);
		if (!stream)
		{
			return false;
		}
		stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
		return static_cast<bool>(stream);
	}
}
