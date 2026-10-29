#include <pch.h>
#include "importers.h"

#if !WI_SHIPPING

#include <sstream>

#include "assets/editor/file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// A 3D color lookup table in the Adobe/Resolve ".cube" text format. Only 3D tables with the default 0..1 domain.
		// Red changes fastest, then green, then blue.
		bool parse_cube(const std::string& text, LutData& lut, std::string& error)
		{
			std::istringstream in(text);
			std::string line;
			bool sizeSeen = false;
			u32 lineNumber = 0;

			while (std::getline(in, line))
			{
				lineNumber++;
				const size_t first = line.find_first_not_of(" \t\r");
				if (first == std::string::npos || line[first] == '#')
				{
					continue;
				}

				std::istringstream fields(line);
				std::string keyword;
				fields >> keyword;

				if (keyword == "TITLE")
				{
					const size_t open = line.find('"');
					const size_t close = line.rfind('"');
					if (open != std::string::npos && close > open)
					{
						lut.Title = line.substr(open + 1, close - open - 1);
					}
					continue;
				}

				if (keyword == "LUT_1D_SIZE")
				{
					error = "1D tables are not supported";
					return false;
				}

				if (keyword == "LUT_3D_SIZE")
				{
					u32 size = 0;
					if (!(fields >> size) || size < 2 || size > 64)
					{
						error = "line " + std::to_string(lineNumber) + ": bad LUT_3D_SIZE (2..64)";
						return false;
					}
					lut.Size = size;
					lut.Table.reserve(static_cast<size_t>(size) * size * size * 3);
					sizeSeen = true;
					continue;
				}

				if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX")
				{
					float r = 0.0f, g = 0.0f, b = 0.0f;
					fields >> r >> g >> b;
					const float expected = (keyword == "DOMAIN_MIN") ? 0.0f : 1.0f;
					if (r != expected || g != expected || b != expected)
					{
						error = "only the default 0..1 domain is supported";
						return false;
					}
					continue;
				}

				if (!sizeSeen)
				{
					error = "line " + std::to_string(lineNumber) + ": data before LUT_3D_SIZE";
					return false;
				}

				float r = 0.0f, g = 0.0f, b = 0.0f;
				std::istringstream numbers(line);
				if (!(numbers >> r >> g >> b))
				{
					error = "line " + std::to_string(lineNumber) + ": expected three numbers";
					return false;
				}

				if (lut.Table.size() >= static_cast<size_t>(lut.Size) * lut.Size * lut.Size * 3)
				{
					error = "line " + std::to_string(lineNumber) + ": more entries than LUT_3D_SIZE says";
					return false;
				}

				lut.Table.push_back(r);
				lut.Table.push_back(g);
				lut.Table.push_back(b);
			}

			if (!sizeSeen)
			{
				error = "no LUT_3D_SIZE";
				return false;
			}

			if (lut.Table.size() != static_cast<size_t>(lut.Size) * lut.Size * lut.Size * 3)
			{
				error = "expected " + std::to_string(lut.Size * lut.Size * lut.Size) + " entries, found " + std::to_string(lut.Table.size() / 3);
				return false;
			}

			return true;
		}

		class LutImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "LutImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".cube" }; }

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				LutData lut;
				std::string error;
				if (!parse_cube(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()), lut, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", ctx.source().filename().string(), error);
					return false;
				}

				ctx.emit(ctx.main_asset(), lut);
				return true;
			}
		};

		class ShaderImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ShaderImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".hlsl", ".hlsli" }; }

			virtual bool import(ImportContext& ctx) override
			{
				std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				ShaderData shader;
				shader.Name = ctx.source().filename().string();
				shader.Source.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());

				ctx.emit(ctx.main_asset(), shader);
				return true;
			}
		};
	}

	Scope<IImporter> create_lut_importer() { return CreateScope<LutImporter>(); }
	Scope<IImporter> create_shader_importer() { return CreateScope<ShaderImporter>(); }
}

#endif
