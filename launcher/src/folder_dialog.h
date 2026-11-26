#pragma once

#include <filesystem>
#include <optional>

namespace wiwa
{
	// The Windows folder picker (IFileOpenDialog with FOS_PICKFOLDERS), opened at `start` when it exists. Nothing when cancelled. Main thread.
	std::optional<std::filesystem::path> pick_folder(const std::filesystem::path& start);
}
