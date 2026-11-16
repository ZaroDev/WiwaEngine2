#include <editor_common.h>
#include "audio_settings_file.h"
#include <core/file_io.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	bool audio_settings_from_text(const std::string& text, AudioProjectSettings& out, std::string& error)
	{
		using Json = nlohmann::json;
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		AudioProjectSettings settings;
		if (const auto backend = root.find("backend"); backend != root.end())
		{
			const std::optional<AudioBackendKind> kind = backend->is_string() ? audio_backend_from_name(backend->get<std::string>()) : std::nullopt;
			if (!kind)
			{
				error = "\"backend\" must be \"Null\", \"Wwise\" or \"MiniAudio\"";
				return false;
			}
			settings.Backend = *kind;
		}

		if (const auto banks = root.find("banks"); banks != root.end())
		{
			if (!banks->is_array())
			{
				error = "\"banks\" must be a list of guid strings";
				return false;
			}
			for (const Json& bank : *banks)
			{
				const std::optional<AssetGuid> guid = bank.is_string() ? AssetGuid::from_string(bank.get<std::string>()) : std::nullopt;
				if (!guid)
				{
					error = "\"banks\" holds something that is not a guid";
					return false;
				}
				AssetRef ref;
				ref.Guid = *guid;
				settings.Banks.push_back(ref);
			}
		}

		if (const auto pool = root.find("memoryPoolKb"); pool != root.end())
		{
			if (!pool->is_number_unsigned())
			{
				error = "\"memoryPoolKb\" must be a whole number";
				return false;
			}
			settings.MemoryPoolKb = static_cast<u32>(std::min<u64>(pool->get<u64>(), 0xFFFFFFFFull));
		}

		if (const auto language = root.find("language"); language != root.end())
		{
			if (!language->is_string())
			{
				error = "\"language\" must be a string";
				return false;
			}
			settings.Language = language->get<std::string>();
		}

		out = sanitized(std::move(settings));
		return true;
	}

	std::string audio_settings_to_text(const AudioProjectSettings& settings)
	{
		nlohmann::json root;
		root["backend"] = audio_backend_name(settings.Backend);
		nlohmann::json banks = nlohmann::json::array();
		for (const AssetRef& bank : settings.Banks)
		{
			banks.push_back(bank.Guid.to_string());
		}
		root["banks"] = banks;
		root["memoryPoolKb"] = settings.MemoryPoolKb;
		root["language"] = settings.Language;
		return root.dump(2);
	}

	bool load_audio_settings(const std::filesystem::path& path, AudioProjectSettings& out, std::string& error)
	{
		out = AudioProjectSettings{};
		std::error_code exists;
		if (!std::filesystem::exists(path, exists))
		{
			return true;
		}

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			error = "cannot read " + path.string();
			return false;
		}
		std::stringstream text;
		text << file.rdbuf();
		if (!audio_settings_from_text(text.str(), out, error))
		{
			out = AudioProjectSettings{};
			return false;
		}
		return true;
	}

	bool save_audio_settings(const std::filesystem::path& path, const AudioProjectSettings& settings, std::string& error)
	{
		const std::string text = audio_settings_to_text(settings);
		return write_file_atomic(path, text.data(), text.size(), error);
	}
}
