#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <assets/asset_guid.h>
#include <assets/asset_source.h>
#include <assets/wasset_io.h>

namespace wiwa
{
	// The project file of the editor's working directory (JSON); the packager puts the same values in assets.wpak and a game reads them there.
	constexpr const char* PROJECT_SETTINGS_FILE = "project.wproject";

	// The key a pack stores the project settings under (it is not an asset, so no real guid is this one).
	constexpr AssetGuid PROJECT_SETTINGS_GUID{ ~0ull, ~0ull - 4 };

	constexpr u32 PROJECT_SETTINGS_VERSION = 1;

	enum class WindowMode : u8
	{
		Windowed,
		Borderless,
		Fullscreen,
	};

	enum class LaunchApi : u8
	{
		Vulkan,
		D3D11,
		D3D12,
	};

	// What names the project and how a game starts. Vsync and the copy queue are not here: they are the project's RenderQuality.
	struct ProjectSettings
	{
		std::string Name = "Wiwa Project";
		std::string Company{};
		u32 VersionMajor = 0;
		u32 VersionMinor = 1;
		u32 VersionPatch = 0;
		std::string VersionLabel{};
		std::string Description{};

		std::string WindowTitle{}; // empty: the name
		u32 WindowWidth = 1280;
		u32 WindowHeight = 720;
		WindowMode Mode = WindowMode::Windowed;
		bool Resizable = true;

		LaunchApi Api = LaunchApi::Vulkan;
		bool DebugDrawOnStart = false;
		bool GpuValidation = false;

		u32 FpsCap = 0;     // 0 = uncapped
		u32 FixedRate = 0;  // physics steps per second a new scene starts with; 0 = the built-in default

		bool operator==(const ProjectSettings&) const = default;
	};

	// Sizes 160..16384, FpsCap 0..1000, FixedRate 0 or 10..480, an empty name gets the default.
	ProjectSettings sanitized(ProjectSettings settings);

	// The window title: WindowTitle, or the name when that is empty.
	std::string window_title(const ProjectSettings& settings);

	// "1.2.3", or "1.2.3-label" when there is a label.
	std::string version_string(const ProjectSettings& settings);

	// The project file (JSON). A missing file gives nullopt and an empty error (use the defaults); a file that cannot be read gives nullopt and
	// the reason. Missing keys keep their defaults, unknown keys are ignored, values are sanitized.
	std::optional<ProjectSettings> load_project_settings(const std::filesystem::path& path, std::string& error);
	bool save_project_settings(const std::filesystem::path& path, const ProjectSettings& settings);

	// A version, then the values. A blob that is not of this version or does not read to the end is refused.
	void archive(BinaryArchive& ar, ProjectSettings& v);

	// The project settings of a source; nullopt if the source has none (a Library, an older pack) or they are damaged.
	std::optional<ProjectSettings> read_project_settings(IAssetSource& source);
}
