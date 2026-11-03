#include <editor_common.h>
#include "physics_settings_file.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	bool physics_layers_from_text(const std::string& text, PhysicsLayers& out, std::string& error)
	{
		using Json = nlohmann::json;
		PhysicsLayers layers;
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		if (const auto names = root.find("layers"); names != root.end())
		{
			if (!names->is_array() || names->size() > PHYSICS_LAYER_COUNT)
			{
				error = "\"layers\" must be a list of at most 32 layers";
				return false;
			}
			for (size_t i = 0; i < names->size(); i++)
			{
				const Json& layer = (*names)[i];
				if (!layer.is_object() || !layer.contains("name") || !layer["name"].is_string())
				{
					error = "every layer must be an object with a \"name\"";
					return false;
				}
				layers.Names[i] = layer["name"].get<std::string>();
			}
			for (size_t i = names->size(); i < PHYSICS_LAYER_COUNT; i++)
			{
				layers.Names[i].clear();
			}
		}

		if (const auto matrix = root.find("matrix"); matrix != root.end())
		{
			if (!matrix->is_array() || matrix->size() != PHYSICS_LAYER_COUNT)
			{
				error = "\"matrix\" must be 32 integers";
				return false;
			}
			for (size_t i = 0; i < PHYSICS_LAYER_COUNT; i++)
			{
				const Json& row = (*matrix)[i];
				if (!row.is_number_unsigned() || row.get<u64>() > 0xFFFFFFFFull)
				{
					error = "\"matrix\" must hold unsigned 32 bit integers";
					return false;
				}
				layers.Matrix[i] = static_cast<u32>(row.get<u64>());
			}
			// symmetric: the pair collides only when both sides say so
			for (u32 a = 0; a < PHYSICS_LAYER_COUNT; a++)
			{
				for (u32 b = a + 1; b < PHYSICS_LAYER_COUNT; b++)
				{
					layers.set_collides(a, b, layers.collides(a, b));
				}
			}
		}

		out = layers;
		return true;
	}

	std::string physics_layers_to_text(const PhysicsLayers& layers)
	{
		using Json = nlohmann::json;
		Json root;
		// the names up to the last one that is used (layer 0 always)
		size_t count = 1;
		for (size_t i = 0; i < PHYSICS_LAYER_COUNT; i++)
		{
			if (!layers.Names[i].empty())
			{
				count = i + 1;
			}
		}
		root["layers"] = Json::array();
		for (size_t i = 0; i < count; i++)
		{
			root["layers"].push_back({ { "name", layers.Names[i] } });
		}
		root["matrix"] = Json::array();
		for (size_t i = 0; i < PHYSICS_LAYER_COUNT; i++)
		{
			root["matrix"].push_back(layers.Matrix[i]);
		}
		return root.dump(2);
	}

	bool load_physics_layers(const std::filesystem::path& path, PhysicsLayers& out, std::string& error)
	{
		out = PhysicsLayers{};
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
		if (!physics_layers_from_text(text.str(), out, error))
		{
			out = PhysicsLayers{};
			return false;
		}
		return true;
	}

	bool save_physics_layers(const std::filesystem::path& path, const PhysicsLayers& layers, std::string& error)
	{
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			file << physics_layers_to_text(layers);
			if (!file)
			{
				error = "cannot write " + temp.string();
				return false;
			}
		}
		std::error_code rename;
		std::filesystem::rename(temp, path, rename);
		if (rename)
		{
			error = "cannot replace " + path.string();
			return false;
		}
		return true;
	}
}
