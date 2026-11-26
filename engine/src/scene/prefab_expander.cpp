#include <pch.h>
#include "prefab_expander.h"

#include <unordered_set>

#include "prefab_node_id.h"

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
			record.Components.push_back(ComponentRecord{ NAME_COMPONENT, { FieldRecord{ "Value", FieldValue(node.Name) } } });
			record.Components.push_back(ComponentRecord{ TRANSFORM_COMPONENT, {
				FieldRecord{ "Position", FieldValue(node.Translation) },
				FieldRecord{ "Rotation", FieldValue(node.Rotation) },
				FieldRecord{ "Scale", FieldValue(node.Scale) } } });
			if (node.Mesh.valid())
			{
				record.Components.push_back(ComponentRecord{ "MeshRenderer", {
					FieldRecord{ "Mesh", FieldValue(node.Mesh) },
					FieldRecord{ "Materials", FieldValue(std::vector<AssetRef>{}) } } });
			}

			if (node.Skeleton.valid())
			{
				record.Components.push_back(ComponentRecord{ "SkinBinding", { FieldRecord{ "Skeleton", FieldValue(node.Skeleton) } } });
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

		// a model with animations plays them: its root gets an Animator, unless it already has one. ModelScale is the model's, never the scene's: it
		// comes from the prefab every time (a scene saved by the editor holds the whole Animator, and would keep the scale of the day it was saved)
		void add_animator(EntityRecord& entity, const PrefabData& prefab)
		{
			if (prefab.Animations.empty())
			{
				return;
			}
			if (ComponentRecord* existing = find_component(entity, "Animator"))
			{
				set_field(*existing, "ModelScale", FieldValue(prefab.ImportScale));
				return;
			}

			AssetRef skeleton{};
			for (const PrefabNode& node : prefab.Nodes)
			{
				if (node.Skeleton.valid())
				{
					skeleton = node.Skeleton;
					break;
				}
			}
			if (skeleton.valid())
			{
				entity.Components.push_back(ComponentRecord{ "Animator", {
					FieldRecord{ "Skeleton", FieldValue(skeleton) },
					FieldRecord{ "Clips", FieldValue(prefab.Animations) },
					FieldRecord{ "Clip", FieldValue(prefab.Animations.front()) } } });
				if (prefab.ImportScale != 1.0f)
				{
					set_field(entity.Components.back(), "ModelScale", FieldValue(prefab.ImportScale));
				}
			}
		}

		struct Expansion
		{
			const PrefabResolver& Resolve;
			EntityGuid RootId{};
			ExpandResult& Result;
			std::vector<PrefabOverride> Queued{};   // the overrides of nested instances, deepest first, with composite node ids
			std::vector<AssetGuid> Stack{};         // the prefabs being expanded, outermost first
		};

		// The id a node has in the frame of the prefab of the instance root. chain holds the stable ids of the instance nodes it sits under,
		// outermost first; the ids nest from the right, compose(outer, compose(middle, inner)), so the id a node has in the prefab that
		// holds it is the one an override stored on an instance of that prefab uses, at any depth.
		u64 wrapped_id(const std::vector<u64>& chain, u64 local)
		{
			u64 id = local;
			for (size_t i = chain.size(); i-- > 0;)
			{
				id = compose_node_id(chain[i], id);
			}
			return id;
		}

		// The nodes of a prefab, parents first, under parentGuid. chain is empty for the prefab of the instance root, and ends with the
		// stable id of the instance's node for a nested one.
		void emit_nodes(Expansion& expansion, const PrefabData& prefab, std::vector<u64>& chain, EntityGuid parentGuid, u32 depth)
		{
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

			size_t reached = 0;
			std::vector<size_t> stack(roots.rbegin(), roots.rend());
			while (!stack.empty())
			{
				const size_t index = stack.back();
				stack.pop_back();

				const PrefabNode& node = prefab.Nodes[index];
				const u64 id = wrapped_id(chain, node.StableId);
				EntityRecord record = node_record(node);
				record.Id = EntityGuid::derive(expansion.RootId, id);
				record.Parent = node.Parent < 0 ? parentGuid : EntityGuid::derive(expansion.RootId, wrapped_id(chain, prefab.Nodes[static_cast<size_t>(node.Parent)].StableId));
				const EntityGuid recordId = record.Id;
				reached++;

				// a nested instance: its nodes follow right after this one (parents first), under this one
				const PrefabData* inner = nullptr;
				if (node.Instance)
				{
					const AssetGuid innerGuid = node.Instance->Prefab.resolved();
					const std::string where = "prefab " + prefab.Name + ": node " + node.Name;
					if (depth + 1 > MAX_PREFAB_DEPTH)
					{
						expansion.Result.Warnings.push_back(where + " holds an instance nested deeper than " + std::to_string(MAX_PREFAB_DEPTH) + " levels, it was left out");
					}
					else if (std::find(expansion.Stack.begin(), expansion.Stack.end(), innerGuid) != expansion.Stack.end())
					{
						expansion.Result.Warnings.push_back(where + " holds an instance of a prefab that contains it (a cycle), it was left out");
					}
					else if ((inner = expansion.Resolve ? expansion.Resolve(node.Instance->Prefab) : nullptr) == nullptr)
					{
						expansion.Result.Warnings.push_back(where + " holds an instance of a prefab that is not available, it has no children");
					}
				}

				if (inner != nullptr)
				{
					add_animator(record, *inner);
				}
				expansion.Result.Entities.push_back(std::move(record));
				expansion.Result.NodeIds.push_back(id);
				expansion.Result.NestedRoots.push_back(node.Instance ? 1 : 0);

				if (inner != nullptr)
				{
					expansion.Stack.push_back(node.Instance->Prefab.resolved());
					chain.push_back(node.StableId);
					emit_nodes(expansion, *inner, chain, recordId, depth + 1);

					// the instance's own overrides address the inner nodes by the ids they have in the inner prefab: 0 is the instance's node.
					// They apply after the ones of the instances inside it, so the outer level has the last word.
					for (PrefabOverride override : node.Instance->Overrides)
					{
						override.NodeId = override.NodeId == 0 ? id : wrapped_id(chain, override.NodeId);
						expansion.Queued.push_back(std::move(override));
					}
					chain.pop_back();
					expansion.Stack.pop_back();
				}

				stack.insert(stack.end(), children[index].rbegin(), children[index].rend());
			}

			if (reached < count)
			{
				expansion.Result.Warnings.push_back("prefab " + prefab.Name + ": " + std::to_string(count - reached) + " node(s) are in a parent loop and were left out");
			}
		}
	}

	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab, const PrefabResolver& resolve)
	{
		ExpandResult result;
		result.Entities.push_back(instanceRoot);
		result.NodeIds.push_back(0);
		result.NestedRoots.push_back(0);
		add_animator(result.Entities[0], prefab);

		Expansion expansion{ resolve, instanceRoot.Id, result };
		if (const ComponentRecord* link = find_component(instanceRoot, PREFAB_INSTANCE_COMPONENT))
		{
			for (const FieldRecord& field : link->Fields)
			{
				if (const AssetRef* ref = std::get_if<AssetRef>(&field.Value); field.Name == "Prefab" && ref != nullptr && ref->valid())
				{
					expansion.Stack.push_back(ref->resolved()); // so that a prefab that holds itself is caught at the first level
				}
			}
		}
		std::vector<u64> chain;
		emit_nodes(expansion, prefab, chain, instanceRoot.Id, 0);

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

		const size_t nestedCount = expansion.Queued.size(); // the overrides of nested instances come first
		std::vector<PrefabOverride> overrides = std::move(expansion.Queued);
		overrides.insert(overrides.end(), instanceRoot.Overrides.begin(), instanceRoot.Overrides.end());
		for (size_t overrideIndex = 0; overrideIndex < overrides.size(); overrideIndex++)
		{
			const PrefabOverride& override = overrides[overrideIndex];
			const bool fromNested = overrideIndex < nestedCount;
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
				// a child that the root's overrides add is told apart by its own id; one that a nested instance's override adds belongs to
				// the node it goes under, so that it has its own guid in every place the instance is used, and is a node of the instance
				// (an edit to it is an override of the root, by this id)
				const u64 childKey = override.Child.Id.Hi ^ override.Child.Id.Lo;
				const u64 childNode = fromNested ? compose_node_id(override.NodeId, childKey) : ADDED_CHILD_NODE;
				child.Id = EntityGuid::derive(instanceRoot.Id, fromNested ? childNode : childKey);
				const EntityGuid parentId = entity.Id; // entity may move when the vector grows
				child.Parent = parentId;
				result.Entities.push_back(std::move(child));
				result.NodeIds.push_back(childNode);
				result.NestedRoots.push_back(0);
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
						result.NestedRoots.erase(result.NestedRoots.begin() + static_cast<std::ptrdiff_t>(i));
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

	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab)
	{
		return expand_prefab(instanceRoot, prefab, PrefabResolver{});
	}

	void collect_nested_prefabs(const PrefabData& prefab, std::vector<AssetRef>& out)
	{
		for (const PrefabNode& node : prefab.Nodes)
		{
			if (node.Instance && node.Instance->Prefab.valid())
			{
				out.push_back(node.Instance->Prefab);
			}
		}
	}

	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field, const PrefabResolver& resolve)
	{
		EntityRecord root;
		root.Id = EntityGuid::derive(EntityGuid{ 1, 1 }, 1); // any id: only the node's fields are read
		const ExpandResult expanded = expand_prefab(root, prefab, resolve);
		for (size_t i = 1; i < expanded.Entities.size(); i++)
		{
			if (expanded.NodeIds[i] != nodeId)
			{
				continue;
			}

			const ComponentRecord* found = find_component(expanded.Entities[i], component);
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

	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field)
	{
		return prefab_value(prefab, nodeId, component, field, PrefabResolver{});
	}

	bool is_field_override(const PrefabOverride& override, u64 nodeId, std::string_view component, std::string_view field)
	{
		return override.Kind == OverrideKind::SetField && override.NodeId == nodeId && override.Component == component && override.Field == field;
	}

	void revert_field(EntityRecord& instanceRoot, u64 nodeId, std::string_view component, std::string_view field)
	{
		std::erase_if(instanceRoot.Overrides, [&](const PrefabOverride& override) { return is_field_override(override, nodeId, component, field); });
	}

	void revert_all(EntityRecord& instanceRoot)
	{
		instanceRoot.Overrides.clear();
	}

	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab, const PrefabResolver& resolve)
	{
		ExpandResult expanded = expand_prefab(instanceRoot, prefab, resolve);
		EntityRecord& root = expanded.Entities[0];
		root.Overrides.clear();
		std::erase_if(root.Components, [](const ComponentRecord& component) { return component.Name == PREFAB_INSTANCE_COMPONENT; });
		return std::move(expanded.Entities);
	}

	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab)
	{
		return unpack(instanceRoot, prefab, PrefabResolver{});
	}
}
