#pragma once

#include <filesystem>

#include "asset_source.h"

namespace wiwa
{
	// Loose cooked files: <root>/<first 2 hex of guid>/<guid>.wasset
	class LibrarySource final : public IAssetSource
	{
	public:
		explicit LibrarySource(std::filesystem::path root) : m_Root(std::move(root)) {}

		virtual std::optional<std::vector<std::byte>> read(AssetGuid guid) override;
		virtual bool contains(AssetGuid guid) const override;

		// Where a cooked asset of the user's Assets/ is; engine assets are in <root>/engine (see locate).
		std::filesystem::path path_of(AssetGuid guid) const;
		// The file that exists, in <root> or <root>/engine; path_of if neither does.
		std::filesystem::path locate(AssetGuid guid) const;
		const std::filesystem::path& get_root() const { return m_Root; }

	private:
		std::filesystem::path m_Root;
	};
}
