#include <pch.h>
#include "scene_clone.h"

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

		// Every component that is not reflected (so not in the ComponentRegistry). A new one is added here, and to
		// scene_records.cpp if it is saved.
		void copy_extra_components(entt::registry& from, entt::entity source, entt::registry& to, entt::entity target)
		{
			copy_value<ValueBags>(from, source, to, target);
			copy_value<PrefabInstance>(from, source, to, target);
			copy_value<PrefabChild>(from, source, to, target);
			copy_value<UnknownComponents>(from, source, to, target);
		}
	}

	Ref<Scene> clone_scene(Scene& source)
	{
		Ref<Scene> clone = CreateRef<Scene>();
		entt::registry& from = source.registry();
		entt::registry& to = clone->registry();

		std::unordered_map<entt::entity, entt::entity> made; // source entity -> its copy

		// parents before children, siblings in order, roots in order: the copy is built in the order it has to have
		source.for_each_in_order([&](entt::entity entity)
			{
				const entt::entity parent = from.get<Hierarchy>(entity).Parent;
				const entt::entity copy = clone->create(from.get<Id>(entity).Guid, from.get<Name>(entity).Value, parent == entt::null ? entt::null : made.at(parent));
				made[entity] = copy;

				for (const ComponentInfo* info : ComponentRegistry::get().all())
				{
					if (info->Name == ID_COMPONENT || !info->Has(from, entity))
					{
						continue;
					}
					info->Add(to, copy);
					for (size_t field = 0; field < info->Fields.size(); field++)
					{
						info->Set(to, copy, field, info->Get(from, entity, field));
					}
				}

				copy_extra_components(from, entity, to, copy);
			});

		clone->set_settings(source.settings());
		clone->update_world_transforms();
		return clone;
	}
}
