#include <editor_common.h>
#include "prefab_file.h"

#include <fstream>
#include <unordered_map>

#include <scene/prefab_node_id.h>

#include "../scene/scene_json.h"
#include "file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		template<typename T>
		const T* field_of(const ComponentRecord& component, std::string_view name)
		{
			for (const FieldRecord& field : component.Fields)
			{
				if (field.Name == name)
				{
					return std::get_if<T>(&field.Value);
				}
			}
			return nullptr;
		}
	}

	std::optional<PrefabData> prefab_from_records(const std::vector<EntityRecord>& entities, std::string name, std::string& error)
	{
		if (entities.empty())
		{
			error = "a prefab needs at least one entity";
			return std::nullopt;
		}

		PrefabData prefab;
		prefab.Name = std::move(name);
		std::unordered_map<EntityGuid, i32> indexOf;

		for (const EntityRecord& entity : entities)
		{
			PrefabNode node;
			node.Name = "Node";
			if (entity.Parent.valid())
			{
				const auto parent = indexOf.find(entity.Parent);
				if (parent == indexOf.end())
				{
					error = "an entity names a parent that is not listed before it";
					return std::nullopt;
				}
				node.Parent = parent->second;
			}

			for (const ComponentRecord& component : entity.Components)
			{
				if (component.Name == "PrefabInstance")
				{
					const AssetRef* inner = field_of<AssetRef>(component, "Prefab");
					if (inner == nullptr || !inner->valid())
					{
						error = "an instance of a prefab names no prefab";
						return std::nullopt;
					}
					node.Instance = PrefabNodeInstance{ *inner, entity.Overrides };
					continue; // the node is the root of the instance; the link is not one of its components
				}
				else if (component.Name == "Id")
				{
					continue; // the node's identity is its StableId
				}
				else if (component.Name == "Name")
				{
					if (const std::string* value = field_of<std::string>(component, "Value"))
					{
						node.Name = *value;
					}
				}
				else if (component.Name == "Transform")
				{
					if (const glm::vec3* position = field_of<glm::vec3>(component, "Position"))
					{
						node.Translation = *position;
					}
					if (const glm::quat* rotation = field_of<glm::quat>(component, "Rotation"))
					{
						node.Rotation = *rotation;
					}
					if (const glm::vec3* scale = field_of<glm::vec3>(component, "Scale"))
					{
						node.Scale = *scale;
					}
				}
				else
				{
					node.Components.push_back(component);
				}
			}

			indexOf.emplace(entity.Id, static_cast<i32>(prefab.Nodes.size()));
			prefab.Nodes.push_back(std::move(node));
		}

		assign_stable_ids(prefab.Nodes);
		return prefab;
	}

	bool write_prefab_file(const std::filesystem::path& file, const std::vector<EntityRecord>& entities, const std::string& name)
	{
		SceneData data;
		data.Name = name;
		data.Entities = entities;
		const std::string text = scene_to_json(data).dump(2);

		std::error_code error;
		if (file.has_parent_path())
		{
			std::filesystem::create_directories(file.parent_path(), error);
		}

		std::filesystem::path temp = file;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			out << text;
			if (!out)
			{
				WI_ASSET_LOG(Error, "Could not write {0}", temp.string());
				return false;
			}
		}

		std::filesystem::rename(temp, file, error);
		if (error)
		{
			WI_ASSET_LOG(Error, "Could not replace {0}: {1}", file.string(), error.message());
			return false;
		}
		return true;
	}

	std::optional<std::vector<EntityRecord>> read_prefab_file(const std::filesystem::path& file, std::string& error, std::vector<std::string>* warnings)
	{
		std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read the file";
			return std::nullopt;
		}

		const char* begin = reinterpret_cast<const char*>(bytes->data());
		SceneJson json = SceneJson::parse(begin, begin + bytes->size(), nullptr, false);
		if (json.is_discarded())
		{
			error = "not valid JSON";
			return std::nullopt;
		}

		SceneJsonResult result = scene_from_json(json);
		if (warnings != nullptr)
		{
			*warnings = std::move(result.Warnings);
		}
		if (!result.Scene)
		{
			error = result.Error;
			return std::nullopt;
		}
		return std::move(result.Scene->Entities);
	}
}
