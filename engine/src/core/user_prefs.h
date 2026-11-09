#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include <core/common.h>

namespace wiwa
{
	// The player's own settings of a game (options the game menu changes): a flat map of names to a bool, a number or a string, kept in
	// user_prefs.json next to the program. Main thread only. A missing or unreadable file gives empty prefs (a warning for the unreadable one).
	class UserPrefs
	{
	public:
		using Value = std::variant<bool, double, std::string>;

		static UserPrefs& get();

		// The file the prefs are read from and saved to; reads it again. Only the tests change it.
		void set_file(const std::filesystem::path& file);

		std::optional<Value> value(const std::string& key);
		void set(const std::string& key, Value value);
		bool save();

	private:
		UserPrefs() = default;
		void load();

		std::filesystem::path m_File = "user_prefs.json";
		bool m_Loaded = false;
		struct Impl;
		Scope<Impl> m_Impl;

	public:
		~UserPrefs();
	};
}
