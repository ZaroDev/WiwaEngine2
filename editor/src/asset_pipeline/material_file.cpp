#include <editor_common.h>
#include "material_file.h"
#include <core/file_io.h>

#include <charconv>
#include <cstdlib>
#include <set>

#include "file_utils.h"

namespace wiwa
{
	namespace
	{
		using Json = nlohmann::json;

		// Every key of the format this version understands; the others go to Extra.
		const std::set<std::string> KNOWN_KEYS = {
			"version", "name", "baseColor", "metallic", "roughness", "emissive", "alpha", "alphaCutoff", "doubleSided",
			"clearcoat", "clearcoatRoughness", "sheen", "sheenRoughness", "transmission", "textures", "shader", "params" };

		bool read_float(const Json& root, const char* key, float& value, std::string& error)
		{
			auto found = root.find(key);
			if (found == root.end())
			{
				return true;
			}
			if (!found->is_number())
			{
				error = std::string("\"") + key + "\" must be a number";
				return false;
			}
			value = found->get<float>();
			return true;
		}

		template<int N>
		bool read_vector(const Json& root, const char* key, glm::vec<N, float>& value, std::string& error)
		{
			auto found = root.find(key);
			if (found == root.end())
			{
				return true;
			}
			if (!found->is_array() || found->size() != static_cast<size_t>(N))
			{
				error = std::string("\"") + key + "\" must be an array of " + std::to_string(N) + " numbers";
				return false;
			}
			for (int i = 0; i < N; i++)
			{
				if (!(*found)[i].is_number())
				{
					error = std::string("\"") + key + "\" must be an array of " + std::to_string(N) + " numbers";
					return false;
				}
				value[i] = (*found)[i].get<float>();
			}
			return true;
		}

		bool read_string(const Json& root, const char* key, std::string& value, std::string& error)
		{
			auto found = root.find(key);
			if (found == root.end())
			{
				return true;
			}
			if (!found->is_string())
			{
				error = std::string("\"") + key + "\" must be a string";
				return false;
			}
			value = found->get<std::string>();
			return true;
		}

		// A float written as the double it widens to shows 0.35f as 0.3499999940395355; this takes the shortest text that
		// reads back as the same float and parses that, so the file says 0.35.
		Json clean(float value)
		{
			char text[48];
			const auto result = std::to_chars(text, text + sizeof(text) - 1, value);
			if (result.ec != std::errc())
			{
				return value;
			}
			*result.ptr = '\0';
			return std::strtod(text, nullptr);
		}

		const char* alpha_to_string(AlphaMode mode)
		{
			switch (mode)
			{
			case AlphaMode::Mask: return "mask";
			case AlphaMode::Blend: return "blend";
			default: return "opaque";
			}
		}

		bool parse_material(const Json& root, MaterialFileData& out, std::string& error)
		{
			if (!root.is_object())
			{
				error = "the file must hold a JSON object";
				return false;
			}

			if (auto version = root.find("version"); version != root.end())
			{
				if (!version->is_number_integer() || version->get<int>() != 1)
				{
					error = "unsupported \"version\" (this editor reads version 1)";
					return false;
				}
			}

			MaterialData& m = out.Material;
			if (!read_string(root, "name", m.Name, error) ||
				!read_vector<4>(root, "baseColor", m.BaseColorFactor, error) ||
				!read_float(root, "metallic", m.MetallicFactor, error) ||
				!read_float(root, "roughness", m.RoughnessFactor, error) ||
				!read_vector<3>(root, "emissive", m.EmissiveFactor, error) ||
				!read_float(root, "alphaCutoff", m.AlphaCutoff, error) ||
				!read_float(root, "clearcoat", m.Clearcoat, error) ||
				!read_float(root, "clearcoatRoughness", m.ClearcoatRoughness, error) ||
				!read_float(root, "sheen", m.SheenIntensity, error) ||
				!read_float(root, "sheenRoughness", m.SheenRoughness, error) ||
				!read_float(root, "transmission", m.Transmission, error) ||
				!read_string(root, "shader", out.ShaderPath, error))
			{
				return false;
			}

			if (auto alpha = root.find("alpha"); alpha != root.end())
			{
				const std::string mode = alpha->is_string() ? alpha->get<std::string>() : std::string();
				if (mode == "opaque") m.Alpha = AlphaMode::Opaque;
				else if (mode == "mask") m.Alpha = AlphaMode::Mask;
				else if (mode == "blend") m.Alpha = AlphaMode::Blend;
				else
				{
					error = "\"alpha\" must be \"opaque\", \"mask\" or \"blend\"";
					return false;
				}
			}

			if (auto doubleSided = root.find("doubleSided"); doubleSided != root.end())
			{
				if (!doubleSided->is_boolean())
				{
					error = "\"doubleSided\" must be true or false";
					return false;
				}
				m.DoubleSided = doubleSided->get<bool>();
			}

			if (auto textures = root.find("textures"); textures != root.end())
			{
				if (!textures->is_object())
				{
					error = "\"textures\" must be an object";
					return false;
				}
				if (!read_string(*textures, "baseColor", out.BaseColorPath, error) || !read_string(*textures, "normal", out.NormalPath, error) ||
					!read_string(*textures, "orm", out.OrmPath, error) || !read_string(*textures, "emissive", out.EmissivePath, error))
				{
					return false;
				}
			}

			if (auto params = root.find("params"); params != root.end())
			{
				if (!params->is_object())
				{
					error = "\"params\" must be an object";
					return false;
				}
				for (auto it = params->begin(); it != params->end(); ++it)
				{
					out.Params[it.key()] = it.value();
				}
			}

			for (auto it = root.begin(); it != root.end(); ++it)
			{
				if (!KNOWN_KEYS.contains(it.key()))
				{
					out.Extra[it.key()] = it.value();
				}
			}
			return true;
		}
	}

	bool read_material_file(const std::filesystem::path& file, MaterialFileData& out, std::string& error)
	{
		out = MaterialFileData{};

		std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read " + file.string();
			return false;
		}

		const char* begin = reinterpret_cast<const char*>(bytes->data());
		Json root = Json::parse(begin, begin + bytes->size(), nullptr, false);
		if (root.is_discarded())
		{
			error = "not valid JSON";
			return false;
		}

		if (!parse_material(root, out, error))
		{
			out = MaterialFileData{};
			return false;
		}
		return true;
	}

	bool write_material_file(const std::filesystem::path& file, const MaterialFileData& data, std::string& error)
	{
		const MaterialData& m = data.Material;

		Json root = data.Extra.is_object() ? data.Extra : Json::object();
		root["version"] = 1;
		root["name"] = m.Name;
		root["baseColor"] = { clean(m.BaseColorFactor.x), clean(m.BaseColorFactor.y), clean(m.BaseColorFactor.z), clean(m.BaseColorFactor.w) };
		root["metallic"] = clean(m.MetallicFactor);
		root["roughness"] = clean(m.RoughnessFactor);
		root["emissive"] = { clean(m.EmissiveFactor.x), clean(m.EmissiveFactor.y), clean(m.EmissiveFactor.z) };
		root["alpha"] = alpha_to_string(m.Alpha);
		root["alphaCutoff"] = clean(m.AlphaCutoff);
		root["doubleSided"] = m.DoubleSided;
		root["clearcoat"] = clean(m.Clearcoat);
		root["clearcoatRoughness"] = clean(m.ClearcoatRoughness);
		root["sheen"] = clean(m.SheenIntensity);
		root["sheenRoughness"] = clean(m.SheenRoughness);
		root["transmission"] = clean(m.Transmission);

		Json textures = Json::object();
		if (!data.BaseColorPath.empty()) textures["baseColor"] = data.BaseColorPath;
		if (!data.NormalPath.empty()) textures["normal"] = data.NormalPath;
		if (!data.OrmPath.empty()) textures["orm"] = data.OrmPath;
		if (!data.EmissivePath.empty()) textures["emissive"] = data.EmissivePath;
		root["textures"] = textures;

		if (!data.ShaderPath.empty())
		{
			root["shader"] = data.ShaderPath;
		}
		if (!data.Params.empty())
		{
			Json params = Json::object();
			for (const auto& [name, value] : data.Params)
			{
				params[name] = value;
			}
			root["params"] = params;
		}

		// Keys come out in alphabetical order (nlohmann::json keeps its objects sorted), so the file is the same for the same data.
		// The text is written atomically, so a reader (the importer, a crash) never sees half a file.
		const std::string text = root.dump(2) + "\n";
		return write_file_atomic(file, text.data(), text.size(), error);
	}

	std::filesystem::path create_material_file(const std::filesystem::path& directory, const std::string& baseName)
	{
		std::error_code exists;
		std::filesystem::path path = directory / (baseName + ".wmat");
		for (int number = 2; std::filesystem::exists(path, exists); number++)
		{
			path = directory / (baseName + " " + std::to_string(number) + ".wmat");
		}

		std::string error;
		if (!write_material_file(path, MaterialFileData{}, error))
		{
			return {};
		}
		return path;
	}
}
