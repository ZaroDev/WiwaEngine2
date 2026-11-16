#include <pch.h>
#include "library_source.h"
#include <core/file_io.h>

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
		return read_file_bytes(path);
	}
}
