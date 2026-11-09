#pragma once

#include <algorithm>
#include <string>

namespace wiwa
{
	inline std::string lower_text(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// A material name as a file name: characters outside [A-Za-z0-9 _.-] become '_'; a name that would be empty, only
	// dots, end in a dot or space, or be a reserved device name on Windows is changed so that it is a plain file.
	inline std::string safe_file_stem(const std::string& name, const char* fallback = "material")
	{
		std::string stem;
		for (char c : name)
		{
			const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '.' || c == '-';
			stem.push_back(allowed ? c : '_');
		}
		if (stem.size() > 100)
		{
			stem.resize(100);
		}
		while (!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
		{
			stem.pop_back();
		}
		if (stem.empty() || stem.find_first_not_of('.') == std::string::npos)
		{
			return fallback;
		}

		static const char* const reserved[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
			"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
		const std::string lowered = lower_text(stem.substr(0, stem.find('.')));
		for (const char* word : reserved)
		{
			if (lowered == word)
			{
				return stem + "_";
			}
		}
		return stem;
	}
}
