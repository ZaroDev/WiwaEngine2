#include <pch.h>
#include "scene_clone.h"

#include <algorithm>
#include <unordered_map>

#include "reflection/component_registry.h"
#include "reflection/value_bag.h"

namespace wiwa
{
	namespace
	{
		// The components the reflection layer does not describe fully (or at all): copied as the values they are.
		template<typename T>
		void copy_value(entt::registry& from, entt::entity source, entt::registry& to, entt::entity target)
		{
			if (const T* value = from.try_get<T>(source))
			{
				to.emplace_or_replace<T>(target, *value);
			}
		}
	}

	Ref<Scene> clone_scene(Scene& source)
	{
		Ref<Scene> clone = CreateRef<Scene>();
		entt::registry& from = source.registry();
		entt::registry& to = clone->registry();

		std::unordered_map<entt::entity, entt::entity> made; // source entity -> its copy

		// parents before children, siblings in order, roots in order: the copy is built in the order it has to have
		std::vector<entt::entity> stack;
		for (auto root = source.roots().rbegin(); root != source.roots().rend(); ++root)
		{
			stack.push_back(*root);
		}

		while (!stack.empty())
		{
			const entt::entity entity = stack.back();
			stack.pop_back();

			const entt::entity parent = from.get<Hierarchy>(entity).Parent;
			const entt::entity copy = clone->create(from.get<Id>(entity).Guid, from.get<Name>(entity).Value, parent == entt::null ? entt::null : made.at(parent));
			made[entity] = copy;

			for (const ComponentInfo* info : ComponentRegistry::get().all())
			{
				if (info->Name == "Id" || !info->Has(from, entity))
				{
					continue;
				}
				info->Add(to, copy);
				for (size_t field = 0; field < info->Fields.size(); field++)
				{
					info->Set(to, copy, field, info->Get(from, entity, field));
				}
			}

			copy_value<ValueBags>(from, entity, to, copy);
			copy_value<PrefabInstance>(from, entity, to, copy);
			copy_value<PrefabChild>(from, entity, to, copy);
			copy_value<UnknownComponents>(from, entity, to, copy);

			const size_t first = stack.size();
			for (entt::entity child = from.get<Hierarchy>(entity).FirstChild; child != entt::null; child = from.get<Hierarchy>(child).NextSibling)
			{
				stack.push_back(child);
			}
			std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(first), stack.end());
		}

		clone->set_settings(source.settings());
		clone->update_world_transforms();
		return clone;
	}
}
