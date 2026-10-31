#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include <assets/asset_data.h>

namespace wiwa
{
	// The content of a .wmat file. Textures and the shader stay as the path or guid strings written in the file; the
	// importer resolves them. Extra holds the keys this version does not know, so saving never loses them.
	struct MaterialFileData
	{
		MaterialData Material{};
		std::string ShaderPath{};
		std::map<std::string, nlohmann::json> Params{};
		std::string BaseColorPath{};
		std::string NormalPath{};
		std::string OrmPath{};
		std::string EmissivePath{};
		nlohmann::json Extra = nlohmann::json::object();
	};

	// Keys that are missing take the defaults of MaterialData. False with a message in error if the file cannot be read or
	// holds a value of the wrong type.
	bool read_material_file(const std::filesystem::path& file, MaterialFileData& out, std::string& error);

	// Writes "version": 1, every known key and the unknown ones, in a fixed key order.
	bool write_material_file(const std::filesystem::path& file, const MaterialFileData& data, std::string& error);
}
