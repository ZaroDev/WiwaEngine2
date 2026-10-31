#include <pch.h>
#include "script_requires.h"

#include <algorithm>
#include <regex>

namespace wiwa
{
	std::string normalize_require_path(std::string path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
		if (!path.ends_with(".lua"))
		{
			path += ".lua";
		}
		return path;
	}

	RequireScan scan_requires(std::string_view source)
	{
		// drop block comments and line comments first, so a commented-out require is not followed
		std::string text(source);
		text = std::regex_replace(text, std::regex(R"(--\[(=*)\[[\s\S]*?\]\1\])"), " ");
		text = std::regex_replace(text, std::regex(R"(--[^\n]*)"), " ");

		RequireScan scan;

		static const std::regex literal(R"(\brequire\s*\(?\s*(["'])([^"']+)\1)");
		for (auto it = std::sregex_iterator(text.begin(), text.end(), literal); it != std::sregex_iterator(); ++it)
		{
			std::string path = normalize_require_path((*it)[2].str());
			if (std::find(scan.Paths.begin(), scan.Paths.end(), path) == scan.Paths.end())
			{
				scan.Paths.push_back(std::move(path));
			}
		}

		// require( followed by something that is not a quote
		static const std::regex dynamic(R"(\brequire\s*\(\s*[^"'\s)])");
		scan.DynamicCalls = static_cast<u32>(std::distance(std::sregex_iterator(text.begin(), text.end(), dynamic), std::sregex_iterator()));
		return scan;
	}
}
