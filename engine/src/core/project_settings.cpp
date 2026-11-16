#include <pch.h>
#include "project_settings.h"
#include <core/file_io.h>

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	namespace
	{
		constexpr const char* AREA = "Project";

		template<typename E>
		E clamp_enum(u8 raw, E last)
		{
			return static_cast<u8>(last) < raw ? E{} : static_cast<E>(raw);
		}
	}

	ProjectSettings sanitized(ProjectSettings settings)
	{
		const ProjectSettings defaults{};
		if (settings.Name.empty())
		{
			settings.Name = defaults.Name;
		}
		settings.WindowWidth = std::clamp(settings.WindowWidth, 160u, 16384u);
		settings.WindowHeight = std::clamp(settings.WindowHeight, 160u, 16384u);
		settings.FpsCap = std::min(settings.FpsCap, 1000u);
		if (settings.FixedRate != 0)
		{
			settings.FixedRate = std::clamp(settings.FixedRate, 10u, 240u);
		}
		settings.Mode = clamp_enum(static_cast<u8>(settings.Mode), WindowMode::Fullscreen);
		settings.Api = clamp_enum(static_cast<u8>(settings.Api), LaunchApi::D3D12);
		return settings;
	}

	std::string window_title(const ProjectSettings& settings)
	{
		return settings.WindowTitle.empty() ? settings.Name : settings.WindowTitle;
	}

	std::string version_string(const ProjectSettings& settings)
	{
		std::string text = std::to_string(settings.VersionMajor) + "." + std::to_string(settings.VersionMinor) + "." + std::to_string(settings.VersionPatch);
		if (!settings.VersionLabel.empty())
		{
			text += "-" + settings.VersionLabel;
		}
		return text;
	}

	std::optional<ProjectSettings> load_project_settings(const std::filesystem::path& path, std::string& error)
	{
		error.clear();
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return std::nullopt; // not there: the defaults, and nothing to report
		}

		nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			error = path.string() + ": not a JSON object";
			return std::nullopt;
		}

		ProjectSettings settings;
		auto text = [](const nlohmann::json& object, const char* key, std::string& out)
		{
			if (object.is_object() && object.contains(key) && object[key].is_string())
			{
				out = object[key].get<std::string>();
			}
		};
		auto count = [](const nlohmann::json& object, const char* key, u32& out)
		{
			if (object.is_object() && object.contains(key) && object[key].is_number_integer())
			{
				out = static_cast<u32>(std::clamp<i64>(object[key].get<i64>(), 0, 1'000'000));
			}
		};
		auto flag = [](const nlohmann::json& object, const char* key, bool& out)
		{
			if (object.is_object() && object.contains(key) && object[key].is_boolean())
			{
				out = object[key].get<bool>();
			}
		};

		text(json, "name", settings.Name);
		text(json, "company", settings.Company);
		text(json, "description", settings.Description);
		if (json.contains("version"))
		{
			const nlohmann::json& version = json["version"];
			count(version, "major", settings.VersionMajor);
			count(version, "minor", settings.VersionMinor);
			count(version, "patch", settings.VersionPatch);
			text(version, "label", settings.VersionLabel);
		}
		if (json.contains("window"))
		{
			const nlohmann::json& window = json["window"];
			text(window, "title", settings.WindowTitle);
			count(window, "width", settings.WindowWidth);
			count(window, "height", settings.WindowHeight);
			flag(window, "resizable", settings.Resizable);
			std::string mode;
			text(window, "mode", mode);
			if (mode == "windowed") { settings.Mode = WindowMode::Windowed; }
			else if (mode == "borderless") { settings.Mode = WindowMode::Borderless; }
			else if (mode == "fullscreen") { settings.Mode = WindowMode::Fullscreen; }
			else if (!mode.empty()) { WI_LOG(Warning, AREA, "{0}: unknown window mode '{1}', windowed is used", path.string(), mode); }
		}
		if (json.contains("launch"))
		{
			const nlohmann::json& launch = json["launch"];
			flag(launch, "debugDraw", settings.DebugDrawOnStart);
			flag(launch, "gpuValidation", settings.GpuValidation);
			std::string api;
			text(launch, "api", api);
			if (api == "vulkan") { settings.Api = LaunchApi::Vulkan; }
			else if (api == "d3d11") { settings.Api = LaunchApi::D3D11; }
			else if (api == "d3d12") { settings.Api = LaunchApi::D3D12; }
			else if (!api.empty()) { WI_LOG(Warning, AREA, "{0}: unknown graphics API '{1}', Vulkan is used", path.string(), api); }
		}
		if (json.contains("performance"))
		{
			const nlohmann::json& performance = json["performance"];
			count(performance, "fpsCap", settings.FpsCap);
			count(performance, "fixedRate", settings.FixedRate);
		}
		return sanitized(settings);
	}

	bool save_project_settings(const std::filesystem::path& path, const ProjectSettings& settings)
	{
		static constexpr const char* MODES[] = { "windowed", "borderless", "fullscreen" };
		static constexpr const char* APIS[] = { "vulkan", "d3d11", "d3d12" };

		nlohmann::ordered_json json = nlohmann::ordered_json::object();
		json["name"] = settings.Name;
		json["company"] = settings.Company;
		json["description"] = settings.Description;
		json["version"] = { { "major", settings.VersionMajor }, { "minor", settings.VersionMinor }, { "patch", settings.VersionPatch }, { "label", settings.VersionLabel } };
		json["window"] = { { "title", settings.WindowTitle }, { "width", settings.WindowWidth }, { "height", settings.WindowHeight },
			{ "mode", MODES[static_cast<u8>(settings.Mode)] }, { "resizable", settings.Resizable } };
		json["launch"] = { { "api", APIS[static_cast<u8>(settings.Api)] }, { "debugDraw", settings.DebugDrawOnStart }, { "gpuValidation", settings.GpuValidation } };
		json["performance"] = { { "fpsCap", settings.FpsCap }, { "fixedRate", settings.FixedRate } };

		const std::string text = json.dump(2) + '\n';
		std::string error;
		return write_file_atomic(path, text.data(), text.size(), error);
	}

	void archive(BinaryArchive& ar, ProjectSettings& v)
	{
		u32 version = PROJECT_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != PROJECT_SETTINGS_VERSION)
		{
			ar.fail();
			return;
		}
		ar.string(v.Name);
		ar.string(v.Company);
		ar.value(v.VersionMajor);
		ar.value(v.VersionMinor);
		ar.value(v.VersionPatch);
		ar.string(v.VersionLabel);
		ar.string(v.Description);
		ar.string(v.WindowTitle);
		ar.value(v.WindowWidth);
		ar.value(v.WindowHeight);
		ar.value(v.Mode);
		ar.value(v.Resizable);
		ar.value(v.Api);
		ar.value(v.DebugDrawOnStart);
		ar.value(v.GpuValidation);
		ar.value(v.FpsCap);
		ar.value(v.FixedRate);
		if (ar.reading())
		{
			if (ar.remaining() != 0)
			{
				ar.fail();
			}
			else if (ar.ok())
			{
				v = sanitized(v);
			}
		}
	}

	std::optional<ProjectSettings> read_project_settings(IAssetSource& source)
	{
		return read_project_blob<ProjectSettings>(source, PROJECT_SETTINGS_GUID);
	}
}
