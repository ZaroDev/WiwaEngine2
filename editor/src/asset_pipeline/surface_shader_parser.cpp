#include <editor_common.h>
#include "surface_shader_parser.h"

#include <cctype>
#include <cstdlib>

namespace wiwa
{
	namespace
	{
		// One line of the file split in the part that is code (comments and the inside of strings blanked) and the text of its
		// "//" comment, if it has one.
		struct SourceLine
		{
			std::string Code{};
			std::string Comment{};
		};

		std::vector<SourceLine> split_lines(const std::string& source)
		{
			std::vector<SourceLine> lines(1);
			enum class State { Code, LineComment, BlockComment, String, Char };
			State state = State::Code;

			for (size_t i = 0; i < source.size(); i++)
			{
				const char c = source[i];
				const char next = i + 1 < source.size() ? source[i + 1] : '\0';
				SourceLine& line = lines.back();

				if (c == '\r')
				{
					continue;
				}
				if (c == '\n')
				{
					if (state == State::LineComment || state == State::String || state == State::Char)
					{
						state = State::Code;
					}
					lines.emplace_back();
					continue;
				}

				switch (state)
				{
				case State::Code:
					if (c == '/' && next == '/')
					{
						state = State::LineComment;
						i++;
						line.Code += "  ";
					}
					else if (c == '/' && next == '*')
					{
						state = State::BlockComment;
						i++;
						line.Code += "  ";
					}
					else if (c == '"')
					{
						state = State::String;
						line.Code += '"';
					}
					else if (c == '\'')
					{
						state = State::Char;
						line.Code += '\'';
					}
					else
					{
						line.Code += c;
					}
					break;
				case State::LineComment:
					line.Comment += c;
					line.Code += ' ';
					break;
				case State::BlockComment:
					if (c == '*' && next == '/')
					{
						state = State::Code;
						i++;
						line.Code += "  ";
					}
					else
					{
						line.Code += ' ';
					}
					break;
				case State::String:
				case State::Char:
					if (c == '\\' && next != '\n' && next != '\r' && next != '\0')
					{
						i++;
						line.Code += "  ";
					}
					else if ((state == State::String && c == '"') || (state == State::Char && c == '\''))
					{
						state = State::Code;
						line.Code += c;
					}
					else
					{
						line.Code += ' ';
					}
					break;
				}
			}
			return lines;
		}

		bool is_word_char(char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
		}

		// Position of the identifier word in text (not part of a longer identifier), or npos.
		size_t find_word(const std::string& text, const std::string& word, size_t from = 0)
		{
			for (size_t at = text.find(word, from); at != std::string::npos; at = text.find(word, at + 1))
			{
				const bool startsWord = at == 0 || !is_word_char(text[at - 1]);
				const bool endsWord = at + word.size() >= text.size() || !is_word_char(text[at + word.size()]);
				if (startsWord && endsWord)
				{
					return at;
				}
			}
			return std::string::npos;
		}

		// True if word is followed (after spaces) by a '(' somewhere in text.
		bool has_call(const std::string& text, const std::string& word)
		{
			for (size_t at = find_word(text, word); at != std::string::npos; at = find_word(text, word, at + 1))
			{
				size_t after = at + word.size();
				while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after])))
				{
					after++;
				}
				if (after < text.size() && text[after] == '(')
				{
					return true;
				}
			}
			return false;
		}

		struct Token
		{
			std::string Text{};
			u32 Line = 0; // 1-based
		};

		// Identifiers (and numbers) and single punctuation characters of the code of lines [first, end).
		std::vector<Token> tokenize(const std::vector<SourceLine>& lines, size_t first)
		{
			std::vector<Token> tokens;
			for (size_t l = first; l < lines.size(); l++)
			{
				const std::string& code = lines[l].Code;
				for (size_t i = 0; i < code.size();)
				{
					if (std::isspace(static_cast<unsigned char>(code[i])))
					{
						i++;
					}
					else if (is_word_char(code[i]))
					{
						size_t end = i;
						while (end < code.size() && is_word_char(code[end]))
						{
							end++;
						}
						tokens.push_back({ code.substr(i, end - i), static_cast<u32>(l + 1) });
						i = end;
					}
					else
					{
						tokens.push_back({ std::string(1, code[i]), static_cast<u32>(l + 1) });
						i++;
					}
				}
			}
			return tokens;
		}

		struct ParseFailure
		{
			u32 Line = 0;
			std::string Message{};
		};

		bool fail(ParseFailure& failure, u32 line, std::string message)
		{
			failure.Line = line;
			failure.Message = std::move(message);
			return false;
		}

		struct TypeInfo
		{
			const char* Name;
			SurfaceParamType Type;
			u32 Size;       // bytes
			u32 Components; // numbers in a default
		};

		const TypeInfo TYPES[] = {
			{ "float", SurfaceParamType::Float, 4, 1 },
			{ "float2", SurfaceParamType::Float2, 8, 2 },
			{ "float3", SurfaceParamType::Float3, 12, 3 },
			{ "float4", SurfaceParamType::Float4, 16, 4 },
			{ "int", SurfaceParamType::Int, 4, 1 },
			{ "bool", SurfaceParamType::Bool, 4, 1 },
		};

		bool parse_number(const std::string& text, float& value)
		{
			if (text.empty())
			{
				return false;
			}
			char* end = nullptr;
			value = std::strtof(text.c_str(), &end);
			return end == text.c_str() + text.size();
		}

		std::string trim(const std::string& text)
		{
			size_t begin = 0;
			size_t end = text.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
			{
				begin++;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
			{
				end--;
			}
			return text.substr(begin, end - begin);
		}

		// Splits "a, b ,c" into trimmed pieces.
		std::vector<std::string> split_arguments(const std::string& text)
		{
			std::vector<std::string> pieces;
			size_t begin = 0;
			for (;;)
			{
				const size_t comma = text.find(',', begin);
				pieces.push_back(trim(text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin)));
				if (comma == std::string::npos)
				{
					break;
				}
				begin = comma + 1;
			}
			return pieces;
		}

		// Reads the annotations of a member out of its line comment: range(a,b) default(v...) color label("x"), in any order.
		bool parse_annotations(const std::string& comment, const TypeInfo& type, SurfaceParam& param, u32 line, ParseFailure& failure)
		{
			bool hasDefault = false;
			bool hasRange = false;
			size_t i = 0;
			while (i < comment.size())
			{
				if (std::isspace(static_cast<unsigned char>(comment[i])) || comment[i] == ',' || comment[i] == ';')
				{
					i++;
					continue;
				}

				size_t end = i;
				while (end < comment.size() && is_word_char(comment[end]))
				{
					end++;
				}
				const std::string word = comment.substr(i, end - i);
				if (word.empty())
				{
					return fail(failure, line, std::string("unexpected '") + comment[i] + "' in the annotations");
				}

				size_t after = end;
				while (after < comment.size() && std::isspace(static_cast<unsigned char>(comment[after])))
				{
					after++;
				}
				const bool hasArguments = after < comment.size() && comment[after] == '(';

				if (word == "color")
				{
					if (hasArguments)
					{
						return fail(failure, line, "color takes no arguments");
					}
					if (type.Type != SurfaceParamType::Float3 && type.Type != SurfaceParamType::Float4)
					{
						return fail(failure, line, "color needs a float3 or a float4, not " + std::string(type.Name));
					}
					param.Color = true;
					i = end;
					continue;
				}

				if (word != "range" && word != "default" && word != "label")
				{
					return fail(failure, line, "unknown annotation '" + word + "' (expected range, default, color or label)");
				}
				if (!hasArguments)
				{
					return fail(failure, line, word + " needs arguments in parentheses");
				}

				// The arguments run to the matching ')', a label may hold one inside its quotes.
				std::string arguments;
				size_t cursor = after + 1;
				bool inQuotes = false;
				bool closed = false;
				for (; cursor < comment.size(); cursor++)
				{
					if (comment[cursor] == '"')
					{
						inQuotes = !inQuotes;
					}
					else if (comment[cursor] == ')' && !inQuotes)
					{
						closed = true;
						break;
					}
					arguments += comment[cursor];
				}
				if (!closed)
				{
					return fail(failure, line, word + " is missing its closing parenthesis");
				}
				i = cursor + 1;

				if (word == "label")
				{
					const std::string text = trim(arguments);
					if (text.size() < 2 || text.front() != '"' || text.back() != '"' || text.find('"', 1) != text.size() - 1)
					{
						return fail(failure, line, "label needs one text in quotes, like label(\"Amount\")");
					}
					param.Label = text.substr(1, text.size() - 2);
				}
				else if (word == "range")
				{
					const std::vector<std::string> pieces = split_arguments(arguments);
					float low = 0.0f;
					float high = 0.0f;
					if (hasRange)
					{
						return fail(failure, line, "range is given twice");
					}
					if (type.Type == SurfaceParamType::Bool)
					{
						return fail(failure, line, "range does not apply to a bool");
					}
					if (pieces.size() != 2 || !parse_number(pieces[0], low) || !parse_number(pieces[1], high))
					{
						return fail(failure, line, "range needs two numbers, like range(0,1)");
					}
					if (low > high)
					{
						return fail(failure, line, "range has a minimum above its maximum");
					}
					hasRange = true;
					param.Min = low;
					param.Max = high;
				}
				else
				{
					const std::vector<std::string> pieces = split_arguments(arguments);
					if (hasDefault)
					{
						return fail(failure, line, "default is given twice");
					}
					if (pieces.size() != type.Components)
					{
						return fail(failure, line, "default needs " + std::to_string(type.Components) + " value(s) for a " + type.Name);
					}
					for (u32 c = 0; c < type.Components; c++)
					{
						float value = 0.0f;
						if (type.Type == SurfaceParamType::Bool && (pieces[c] == "true" || pieces[c] == "false"))
						{
							value = pieces[c] == "true" ? 1.0f : 0.0f;
						}
						else if (!parse_number(pieces[c], value))
						{
							return fail(failure, line, "default has '" + pieces[c] + "', which is not a number");
						}
						if (type.Type == SurfaceParamType::Int && value != static_cast<float>(static_cast<i64>(value)))
						{
							return fail(failure, line, "default of an int must be a whole number");
						}
						if (type.Type == SurfaceParamType::Bool && value != 0.0f && value != 1.0f)
						{
							return fail(failure, line, "default of a bool is true, false, 0 or 1");
						}
						param.Default[c] = value;
					}
					hasDefault = true;
				}
			}
			return true;
		}

		// Parses the struct body that starts after the '{' at tokens[open]. Returns the index of the closing '}'.
		bool parse_members(const std::vector<Token>& tokens, size_t open, const std::vector<SourceLine>& lines, ParsedSurface& out, size_t& close, ParseFailure& failure)
		{
			size_t i = open + 1;
			u32 end = 0; // bytes used so far
			while (i < tokens.size() && tokens[i].Text != "}")
			{
				const Token& typeToken = tokens[i];
				const TypeInfo* type = nullptr;
				for (const TypeInfo& candidate : TYPES)
				{
					if (typeToken.Text == candidate.Name)
					{
						type = &candidate;
					}
				}
				if (type == nullptr)
				{
					return fail(failure, typeToken.Line, "unknown type '" + typeToken.Text + "' (expected float, float2, float3, float4, int or bool)");
				}
				if (i + 2 >= tokens.size() || !is_word_char(tokens[i + 1].Text[0]))
				{
					return fail(failure, typeToken.Line, "expected a name after " + typeToken.Text);
				}
				const Token& nameToken = tokens[i + 1];
				if (tokens[i + 2].Text != ";")
				{
					return fail(failure, nameToken.Line, "expected ';' after " + nameToken.Text + " (arrays and initializers are not supported)");
				}
				for (const SurfaceParam& other : out.Params)
				{
					if (other.Name == nameToken.Text)
					{
						return fail(failure, nameToken.Line, "parameter '" + nameToken.Text + "' is declared twice");
					}
				}

				// HLSL cbuffer packing: a member never straddles a 16-byte boundary
				u32 offset = end;
				if (offset % 16 + type->Size > 16)
				{
					offset = (offset + 15) / 16 * 16;
				}
				end = offset + type->Size;
				if (end > SURFACE_BLOCK_MAX_SIZE)
				{
					return fail(failure, nameToken.Line, "the parameter block would be larger than " + std::to_string(SURFACE_BLOCK_MAX_SIZE) + " bytes (the limit) at '" + nameToken.Text + "'");
				}

				SurfaceParam param;
				param.Name = nameToken.Text;
				param.Type = type->Type;
				param.Offset = offset;
				if (!parse_annotations(lines[nameToken.Line - 1].Comment, *type, param, nameToken.Line, failure))
				{
					return false;
				}
				out.Params.push_back(std::move(param));
				i += 3;
			}

			if (i >= tokens.size())
			{
				return fail(failure, tokens[open].Line, "the MaterialParams struct is not closed");
			}
			out.BlockSize = (end + 15) / 16 * 16;
			close = i;
			return true;
		}

		bool parse_all(const std::string& source, ParsedSurface& out, ParseFailure& failure)
		{
			const std::vector<SourceLine> lines = split_lines(source);
			std::string allCode;
			for (const SourceLine& line : lines)
			{
				allCode += line.Code;
				allCode += '\n';
			}

			// The MaterialParams struct
			{
				const std::vector<Token> tokens = tokenize(lines, 0);
				for (size_t t = 0; t + 1 < tokens.size(); t++)
				{
					if (tokens[t].Text != "struct" || tokens[t + 1].Text != "MaterialParams")
					{
						continue;
					}
					if (t + 2 >= tokens.size() || tokens[t + 2].Text != "{")
					{
						return fail(failure, tokens[t + 1].Line, "expected '{' after struct MaterialParams");
					}
					size_t close = 0;
					if (!parse_members(tokens, t + 2, lines, out, close, failure))
					{
						return false;
					}

					// The declaration as written, comments blanked
					const u32 firstLine = tokens[t].Line;
					const u32 lastLine = tokens[close].Line;
					for (u32 l = firstLine; l <= lastLine; l++)
					{
						out.ParamsStructSource += lines[l - 1].Code;
						out.ParamsStructSource += '\n';
					}
					break;
				}
			}

			// Textures: "Texture2D Name; // @param"
			for (size_t l = 0; l < lines.size(); l++)
			{
				const std::string& code = lines[l].Code;
				const size_t at = find_word(code, "Texture2D");
				if (at == std::string::npos || lines[l].Comment.find("@param") == std::string::npos)
				{
					continue;
				}
				size_t begin = at + 9;
				while (begin < code.size() && std::isspace(static_cast<unsigned char>(code[begin])))
				{
					begin++;
				}
				size_t end = begin;
				while (end < code.size() && is_word_char(code[end]))
				{
					end++;
				}
				size_t semicolon = end;
				while (semicolon < code.size() && std::isspace(static_cast<unsigned char>(code[semicolon])))
				{
					semicolon++;
				}
				if (end == begin || semicolon >= code.size() || code[semicolon] != ';')
				{
					return fail(failure, static_cast<u32>(l + 1), "a Texture2D marked @param must look like 'Texture2D Name;'");
				}
				if (out.Textures.size() >= SURFACE_MAX_TEXTURES)
				{
					return fail(failure, static_cast<u32>(l + 1), "at most " + std::to_string(SURFACE_MAX_TEXTURES) + " textures per shader");
				}
				out.Textures.push_back(code.substr(begin, end - begin));
			}

			if (!has_call(allCode, "Surface"))
			{
				return fail(failure, 1, "the shader has no Surface function (void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o))");
			}
			out.HasVertexOffset = has_call(allCode, "VertexOffset");
			return true;
		}
	}

	bool parse_surface_shader(const std::string& source, ParsedSurface& out, std::string& error)
	{
		out = ParsedSurface{};
		error.clear();

		ParseFailure failure;
		if (parse_all(source, out, failure))
		{
			return true;
		}

		out = ParsedSurface{};
		error = std::to_string(failure.Line) + ": " + failure.Message;
		return false;
	}
}
