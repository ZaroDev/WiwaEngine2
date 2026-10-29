#include <pch.h>
#include "library_source.h"

#include <fstream>

#include "asset_catalog.h"

namespace wiwa
{
	std::filesystem::path LibrarySource::path_of(AssetGuid guid) const
	{
		const std::string name = guid.to_string();
		return m_Root / name.substr(0, 2) / (name + ".wasset");
	}

	std::filesystem::path LibrarySource::locate(AssetGuid guid) const
	{
		const std::string name = guid.to_string();
		const std::filesystem::path relative = std::filesystem::path(name.substr(0, 2)) / (name + ".wasset");

		std::error_code error;
		for (const std::filesystem::path& folder : { m_Root, m_Root / "engine" })
		{
			if (std::filesystem::is_regular_file(folder / relative, error))
			{
				return folder / relative;
			}
		}
		return path_of(guid);
	}

	bool LibrarySource::contains(AssetGuid guid) const
	{
		std::error_code error;
		return std::filesystem::is_regular_file(locate(guid), error);
	}

	std::optional<std::vector<std::byte>> LibrarySource::read_catalog()
	{
		return read_file(m_Root / CATALOG_FILE_NAME);
	}

	std::optional<std::vector<std::byte>> LibrarySource::read(AssetGuid guid)
	{
		return read_file(locate(guid));
	}

	std::optional<std::vector<std::byte>> LibrarySource::read_file(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return std::nullopt;
		}

		const std::streamsize size = file.tellg();
		if (size < 0)
		{
			return std::nullopt;
		}

		std::vector<std::byte> bytes(static_cast<size_t>(size));
		file.seekg(0);
		if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
		{
			return std::nullopt;
		}

		return bytes;
	}
}
