#include <editor_common.h>
#include "asset_summary.h"

#include <assets/native_anim_files.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/file_utils.h>
#include <asset_pipeline/meta_file.h>

namespace wiwa
{
	namespace
	{
		// The model that holds the <Model>_animation folder the file is in (the folder may be that folder's "clips" or "meshes").
		std::optional<std::filesystem::path> model_of(const std::filesystem::path& file, std::filesystem::path& folder)
		{
			folder = file.parent_path();
			if (folder.filename() == "clips" || folder.filename() == "meshes")
			{
				folder = folder.parent_path();
			}
			const std::string name = folder.filename().string();
			const std::string suffix = "_animation";
			if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
			{
				return std::nullopt;
			}
			const std::string stem = name.substr(0, name.size() - suffix.size());
			std::error_code error;
			for (std::filesystem::directory_iterator it(folder.parent_path(), error), end; !error && it != end; it.increment(error))
			{
				const std::filesystem::path& candidate = it->path();
				const std::string extension = candidate.extension().string();
				if (candidate.stem().string() == stem && (extension == ".fbx" || extension == ".glb" || extension == ".gltf"))
				{
					return candidate;
				}
			}
			return std::nullopt;
		}
	}

	std::string summarize_animation_file(const std::filesystem::path& file)
	{
		const std::filesystem::path extension = file.extension();
		const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			return {};
		}
		std::string reason;
		char text[160];
		if (extension == ".wskel")
		{
			const std::optional<SkeletonData> skeleton = read_skeleton_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), reason);
			if (!skeleton)
			{
				return {};
			}
			std::snprintf(text, sizeof(text), "%zu joints, %zu sockets", skeleton->JointNames.size(), skeleton->Sockets.size());
			return text;
		}
		if (extension == ".wgraph")
		{
			std::optional<AnimationGraphData> graph;
			try
			{
				graph = read_graph_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), reason, false);
			}
			catch (const std::exception&)
			{
				return {}; // a value of the wrong kind
			}
			if (!graph)
			{
				return {};
			}
			size_t states = 0;
			for (const GraphLayer& layer : graph->Layers)
			{
				states += layer.States.size();
			}
			std::snprintf(text, sizeof(text), "%zu layers, %zu states, %zu parameters", graph->Layers.size(), states, graph->Parameters.size());
			return text;
		}
		if (extension == ".wanim")
		{
			const std::optional<AnimationClipData> clip = read_clip_file(*bytes, reason);
			if (!clip)
			{
				return {};
			}
			std::snprintf(text, sizeof(text), "%.2f s, %zu channels, %zu events", clip->Duration, clip->Channels.size(), clip->Events.size());
			return text;
		}
		if (extension == ".wmesh")
		{
			const std::optional<MeshData> mesh = read_mesh_file(*bytes, reason);
			if (!mesh)
			{
				return {};
			}
			std::snprintf(text, sizeof(text), "%zu vertices, %zu triangles, %zu joints", mesh->Vertices.size(), mesh->Indices.size() / 3, mesh->SkinJointNames.size());
			return text;
		}
		return {};
	}

	std::string extracted_from(const std::filesystem::path& file)
	{
		std::filesystem::path folder;
		const std::optional<std::filesystem::path> model = model_of(file, folder);
		return model ? model->filename().string() : std::string{};
	}

	std::string reextract_from_model(AssetDatabase& database, const std::filesystem::path& file)
	{
		std::filesystem::path folder;
		const std::optional<std::filesystem::path> model = model_of(file, folder);
		if (!model)
		{
			return "this file is not in the <Model>_animation folder of a model";
		}
		std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(*model));
		if (!meta || !meta->Settings.is_object() || !meta->Settings.contains("extracted") || !meta->Settings["extracted"].is_object())
		{
			return "the model has not extracted anything yet";
		}

		// the model forgets the entries that name this file: the importer then treats it as new, and a file that exists is never written over
		const std::string wanted = file.lexically_normal().lexically_relative(model->parent_path().lexically_normal()).generic_string();
		nlohmann::json& table = meta->Settings["extracted"];
		std::vector<std::string> forget;
		for (auto it = table.begin(); it != table.end(); ++it)
		{
			if (it.value().is_object() && it.value().contains("file") && it.value()["file"].is_string())
			{
				std::string name = it.value()["file"].get<std::string>();
				std::string lowered = wanted;
				std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (name == lowered)
				{
					forget.push_back(it.key());
				}
			}
		}
		if (forget.empty())
		{
			return "the model's import does not know this file";
		}
		for (const std::string& key : forget)
		{
			table.erase(key);
		}
		if (!meta->save(MetaFile::meta_path_for(*model)))
		{
			return "cannot write the model's .meta";
		}
		database.reimport(*model, true);
		return {};
	}
}
