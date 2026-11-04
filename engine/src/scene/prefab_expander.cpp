#include <pch.h>
#include "prefab_expander.h"

#include <unordered_set>

namespace wiwa
{
	namespace
	{
		ComponentRecord* find_component(EntityRecord& entity, std::string_view name)
		{
			for (ComponentRecord& component : entity.Components)
			{
				if (component.Name == name)
				{
					return &component;
				}
			}
			return nullptr;
		}

		const ComponentRecord* find_component(const EntityRecord& entity, std::string_view name)
		{
			return find_component(const_cast<EntityRecord&>(entity), name);
		}

		// The record of a prefab node, as the prefab alone describes it.
		EntityRecord node_record(const PrefabNode& node)
		{
			EntityRecord record;
			record.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(node.Name) } } });
			record.Components.push_back(ComponentRecord{ "Transform", {
				FieldRecord{ "Position", FieldValue(node.Translation) },
				FieldRecord{ "Rotation", FieldValue(node.Rotation) },
				FieldRecord{ "Scale", FieldValue(node.Scale) } } });
			if (node.Mesh.valid())
			{
				record.Components.push_back(ComponentRecord{ "MeshRenderer", {
					FieldRecord{ "Mesh", FieldValue(node.Mesh) },
					FieldRecord{ "Materials", FieldValue(std::vector<AssetRef>{}) } } });
			}

			for (const ComponentRecord& extra : node.Components)
			{
				if (ComponentRecord* existing = find_component(record, extra.Name))
				{
					*existing = extra;
				}
				else
				{
					record.Components.push_back(extra);
				}
			}
			return record;
		}

		void set_field(ComponentRecord& component, const std::string& name, const FieldValue& value)
		{
			for (FieldRecord& field : component.Fields)
			{
				if (field.Name == name)
				{
					field.Value = value;
					return;
				}
			}
			component.Fields.push_back(FieldRecord{ name, value });
		}
	}

	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab)
	{
		ExpandResult result;
		result.Entities.push_back(instanceRoot);
		result.NodeIds.push_back(0);

		// the nodes, parents first: a depth-first walk from the top-level nodes (a node in a parent loop is never reached)
		const size_t count = prefab.Nodes.size();
		std::vector<std::vector<size_t>> children(count);
		std::vector<size_t> roots;
		for (size_t i = 0; i < count; i++)
		{
			const i32 parent = prefab.Nodes[i].Parent;
			if (parent < 0)
			{
				roots.push_back(i);
			}
			else if (static_cast<size_t>(parent) < count)
			{
				children[static_cast<size_t>(parent)].push_back(i);
			}
		}

		std::unordered_set<u64> prefabNodes;
		std::vector<size_t> stack(roots.rbegin(), roots.rend());
		while (!stack.empty())
		{
			const size_t index = stack.back();
			stack.pop_back();

			const PrefabNode& node = prefab.Nodes[index];
			EntityRecord record = node_record(node);
			record.Id = EntityGuid::derive(instanceRoot.Id, node.StableId);
			record.Parent = node.Parent < 0 ? instanceRoot.Id : EntityGuid::derive(instanceRoot.Id, prefab.Nodes[static_cast<size_t>(node.Parent)].StableId);
			result.Entities.push_back(std::move(record));
			result.NodeIds.push_back(node.StableId);
			prefabNodes.insert(node.StableId);

			stack.insert(stack.end(), children[index].rbegin(), children[index].rend());
		}
		if (prefabNodes.size() < count)
		{
			result.Warnings.push_back("prefab " + prefab.Name + ": " + std::to_string(count - prefabNodes.size()) + " node(s) are in a parent loop and were left out");
		}

		auto index_of_node = [&](u64 nodeId) -> std::optional<size_t>
		{
			for (size_t i = 0; i < result.NodeIds.size(); i++)
			{
				if (result.NodeIds[i] == nodeId)
				{
					return i;
				}
			}
			return std::nullopt;
		};

		std::unordered_set<u64> removed;
		auto drop = [&](u64 nodeId, const char* what)
		{
			if (std::find(result.DroppedOverrideNodes.begin(), result.DroppedOverrideNodes.end(), nodeId) == result.DroppedOverrideNodes.end())
			{
				result.DroppedOverrideNodes.push_back(nodeId);
				result.Warnings.push_back("prefab " + prefab.Name + ": " + what + " on node " + std::to_string(nodeId) + ", which the prefab does not have (any more), was dropped");
			}
		};

		for (const PrefabOverride& override : instanceRoot.Overrides)
		{
			if (removed.contains(override.NodeId))
			{
				continue; // the node was removed by an earlier override: nothing left to change
			}

			const std::optional<size_t> target = index_of_node(override.NodeId);
			if (!target)
			{
				drop(override.NodeId, "an override");
				continue;
			}

			EntityRecord& entity = result.Entities[*target];
			switch (override.Kind)
			{
			case OverrideKind::SetField:
			{
				ComponentRecord* component = find_component(entity, override.Component);
				if (component == nullptr && override.NodeId == 0)
				{
					// the instance root is an ordinary entity: a record that leaves out its Transform means the default one
					entity.Components.push_back(ComponentRecord{ override.Component, {} });
					component = &entity.Components.back();
				}
				if (component == nullptr)
				{
					result.Warnings.push_back("prefab " + prefab.Name + ": node " + std::to_string(override.NodeId) + " has no " + override.Component + ", the override of " + override.Field + " was ignored");
					break;
				}
				set_field(*component, override.Field, override.Value);
				break;
			}
			case OverrideKind::AddComponent:
				if (ComponentRecord* existing = find_component(entity, override.Added.Name))
				{
					*existing = override.Added;
				}
				else
				{
					entity.Components.push_back(override.Added);
				}
				break;
			case OverrideKind::RemoveComponent:
				std::erase_if(entity.Components, [&](const ComponentRecord& component) { return component.Name == override.Component; });
				break;
			case OverrideKind::AddChild:
			{
				EntityRecord child = override.Child;
				child.Overrides.clear();
				child.Parent = entity.Id;
				child.Id = EntityGuid::derive(instanceRoot.Id, override.Child.Id.Hi ^ override.Child.Id.Lo);
				const EntityGuid parentId = entity.Id; // entity may move when the vector grows
				child.Parent = parentId;
				result.Entities.push_back(std::move(child));
				result.NodeIds.push_back(ADDED_CHILD_NODE);
				break;
			}
			case OverrideKind::RemoveNode:
			{
				if (override.NodeId == 0)
				{
					result.Warnings.push_back("prefab " + prefab.Name + ": the instance root cannot be removed by an override");
					break;
				}

				// the node and everything below it
				std::unordered_set<EntityGuid> doomed{ entity.Id };
				for (bool grew = true; grew;)
				{
					grew = false;
					for (const EntityRecord& other : result.Entities)
					{
						if (other.Parent.valid() && doomed.contains(other.Parent) && doomed.insert(other.Id).second)
						{
							grew = true;
						}
					}
				}

				for (size_t i = 0; i < result.Entities.size();)
				{
					if (doomed.contains(result.Entities[i].Id))
					{
						removed.insert(result.NodeIds[i]);
						result.Entities.erase(result.Entities.begin() + static_cast<std::ptrdiff_t>(i));
						result.NodeIds.erase(result.NodeIds.begin() + static_cast<std::ptrdiff_t>(i));
					}
					else
					{
						i++;
					}
				}
				break;
			}
			}
		}

		return result;
	}

	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field)
	{
		for (const PrefabNode& node : prefab.Nodes)
		{
			if (node.StableId != nodeId)
			{
				continue;
			}

			const EntityRecord record = node_record(node);
			const ComponentRecord* found = find_component(record, component);
			if (found == nullptr)
			{
				return std::nullopt;
			}

			for (const FieldRecord& candidate : found->Fields)
			{
				if (candidate.Name == field)
				{
					return candidate.Value;
				}
			}
			return std::nullopt;
		}
		return std::nullopt;
	}

	void revert_field(EntityRecord& instanceRoot, u64 nodeId, std::string_view component, std::string_view field)
	{
		std::erase_if(instanceRoot.Overrides, [&](const PrefabOverride& override)
			{
				return override.Kind == OverrideKind::SetField && override.NodeId == nodeId && override.Component == component && override.Field == field;
			});
	}

	void revert_all(EntityRecord& instanceRoot)
	{
		instanceRoot.Overrides.clear();
	}

	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab)
	{
		ExpandResult expanded = expand_prefab(instanceRoot, prefab);
		EntityRecord& root = expanded.Entities[0];
		root.Overrides.clear();
		std::erase_if(root.Components, [](const ComponentRecord& component) { return component.Name == "PrefabInstance"; });
		return std::move(expanded.Entities);
	}
}
