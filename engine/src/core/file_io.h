#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiwa
{
	// Written next to the target and renamed over it, so that a reader (an importer, the watcher) never sees half a file. Makes the folder if
	// it is missing. On failure the target is untouched, the temporary file is removed and error says why.
	bool write_file_atomic(const std::filesystem::path& path, const void* data, size_t size, std::string& error);

	inline bool write_file_atomic(const std::filesystem::path& path, const std::string& text, std::string& error)
	{
		return write_file_atomic(path, text.data(), text.size(), error);
	}

	// The whole file, or nullopt if it cannot be opened or read.
	std::optional<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path);
}
