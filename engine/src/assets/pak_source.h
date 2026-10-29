#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "asset_source.h"
#include "pak_format.h"

namespace wiwa
{
	// Reads blobs from assets.wpak. The index is loaded when the pack is opened; a pack with a bad header or index is
	// rejected (valid() is false) and every read fails.
	class PakSource final : public IAssetSource
	{
	public:
		explicit PakSource(const std::filesystem::path& file);

		bool valid() const { return m_Valid; }

		virtual std::optional<std::vector<std::byte>> read(AssetGuid guid) override;
		virtual bool contains(AssetGuid guid) const override { return m_Index.contains(guid); }

		const std::unordered_map<AssetGuid, PakEntry>& get_index() const { return m_Index; }

	private:
		std::ifstream m_File;
		std::mutex m_Mutex;
		std::unordered_map<AssetGuid, PakEntry> m_Index;
		u64 m_FileSize = 0;
		bool m_Valid = false;
	};
}
