#include <editor_common.h>
#include "lua_stubs.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace wiwa
{
	bool is_lua_stub_file(const std::filesystem::path& file)
	{
		if (file.extension() != ".lua")
		{
			return false;
		}

		std::ifstream stream(file, std::ios::binary);
		std::string line;
		return stream && std::getline(stream, line) && line.starts_with("---@meta");
	}

	namespace
	{
		bool is_name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
		bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		// The identifier at `pos`, which moves past it. Empty if there is none.
		std::string read_name(const std::string& line, size_t& pos)
		{
			const size_t start = pos;
			while (pos < line.size() && is_name_char(line[pos]))
			{
				pos++;
			}
			return line.substr(start, pos - start);
		}
	}

	std::set<std::string> declared_globals(const std::string& stubSource)
	{
		std::set<std::string> names;
		std::istringstream lines(stubSource);
		std::string line;
		while (std::getline(lines, line))
		{
			if (line.empty() || !is_name_start(line[0]))
			{
				continue; // blank, a comment, or indented (inside a block)
			}

			size_t pos = 0;
			const std::string first = read_name(line, pos);
			if (first == "local")
			{
				continue;
			}

			if (first == "function")
			{
				while (pos < line.size() && line[pos] == ' ')
				{
					pos++;
				}
				const std::string name = read_name(line, pos);
				if (!name.empty() && pos < line.size() && line[pos] == '(')
				{
					names.insert(name); // function name(...): a member (name.m, name:m) declares no global
				}
				continue;
			}

			while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
			{
				pos++;
			}
			if (pos < line.size() && line[pos] == '=' && (pos + 1 >= line.size() || line[pos + 1] != '='))
			{
				names.insert(first);
			}
		}
		return names;
	}
}
