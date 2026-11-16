#include <pch.h>
#include "user_prefs.h"
#include <core/file_io.h>

#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

namespace wiwa
{
	struct UserPrefs::Impl
	{
		std::map<std::string, Value> Values;
	};

	UserPrefs& UserPrefs::get()
	{
		static UserPrefs prefs;
		return prefs;
	}

	UserPrefs::~UserPrefs() = default;

	void UserPrefs::set_file(const std::filesystem::path& file)
	{
		m_File = file;
		m_Loaded = false;
	}

	void UserPrefs::load()
	{
		if (m_Loaded)
		{
			return;
		}
		m_Loaded = true;
		m_Impl = CreateScope<Impl>();

		std::ifstream file(m_File, std::ios::binary);
		if (!file)
		{
			return;
		}
		const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			WI_WARN("Prefs", "{0} is not a JSON object, the prefs start empty", m_File.string());
			return;
		}
		for (const auto& [key, item] : json.items())
		{
			if (item.is_boolean())
			{
				m_Impl->Values[key] = item.get<bool>();
			}
			else if (item.is_number())
			{
				m_Impl->Values[key] = item.get<double>();
			}
			else if (item.is_string())
			{
				m_Impl->Values[key] = item.get<std::string>();
			}
		}
	}

	std::optional<UserPrefs::Value> UserPrefs::value(const std::string& key)
	{
		load();
		const auto found = m_Impl->Values.find(key);
		return found == m_Impl->Values.end() ? std::nullopt : std::optional<Value>(found->second);
	}

	void UserPrefs::set(const std::string& key, Value value)
	{
		load();
		m_Impl->Values[key] = std::move(value);
	}

	bool UserPrefs::save()
	{
		load();
		nlohmann::json json = nlohmann::json::object();
		for (const auto& [key, value] : m_Impl->Values)
		{
			if (const bool* flag = std::get_if<bool>(&value))
			{
				json[key] = *flag;
			}
			else if (const double* number = std::get_if<double>(&value))
			{
				json[key] = *number;
			}
			else
			{
				json[key] = std::get<std::string>(value);
			}
		}
		const std::string text = json.dump(2) + "\n";
		std::string error;
		return write_file_atomic(m_File, text.data(), text.size(), error);
	}
}
