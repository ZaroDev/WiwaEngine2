#include <pch.h>
#include "value_bag.h"

namespace wiwa
{
	namespace
	{
		ValueBag* find_bag(entt::registry& registry, entt::entity entity, const std::string& name)
		{
			ValueBags* bags = registry.try_get<ValueBags>(entity);
			if (bags == nullptr)
			{
				return nullptr;
			}

			for (ValueBag& bag : bags->Bags)
			{
				if (bag.Name == name)
				{
					return &bag;
				}
			}
			return nullptr;
		}
	}

	ComponentInfo make_value_bag_info(std::string name, std::vector<FieldDescriptor> fields)
	{
		ComponentInfo info;
		info.Name = std::move(name);
		info.Fields = std::move(fields);
		auto shared = std::make_shared<const ComponentInfo>(info); // the closures keep their own copy of the fields

		info.Has = [shared](entt::registry& registry, entt::entity entity)
		{
			return find_bag(registry, entity, shared->Name) != nullptr;
		};

		info.Add = [shared](entt::registry& registry, entt::entity entity)
		{
			if (find_bag(registry, entity, shared->Name) != nullptr)
			{
				return;
			}

			ValueBag bag;
			bag.Name = shared->Name;
			for (const FieldDescriptor& field : shared->Fields)
			{
				bag.Values[field.Name] = field.Default;
			}
			registry.get_or_emplace<ValueBags>(entity).Bags.push_back(std::move(bag));
		};

		info.Remove = [shared](entt::registry& registry, entt::entity entity)
		{
			ValueBags* bags = registry.try_get<ValueBags>(entity);
			if (bags == nullptr)
			{
				return;
			}

			std::erase_if(bags->Bags, [&](const ValueBag& bag) { return bag.Name == shared->Name; });
			if (bags->Bags.empty())
			{
				registry.remove<ValueBags>(entity);
			}
		};

		info.Get = [shared](entt::registry& registry, entt::entity entity, size_t index) -> FieldValue
		{
			if (index >= shared->Fields.size())
			{
				return FieldValue{};
			}

			const FieldDescriptor& field = shared->Fields[index];
			if (const ValueBag* bag = find_bag(registry, entity, shared->Name))
			{
				const auto it = bag->Values.find(field.Name);
				if (it != bag->Values.end())
				{
					return it->second;
				}
			}
			return field.Default;
		};

		info.Set = [shared](entt::registry& registry, entt::entity entity, size_t index, const FieldValue& value)
		{
			ValueBag* bag = find_bag(registry, entity, shared->Name);
			if (bag == nullptr || index >= shared->Fields.size() || !is_valid_field_value(shared->Fields[index], value))
			{
				return false;
			}

			bag->Values[shared->Fields[index].Name] = value;
			return true;
		};

		return info;
	}

	void reconcile_bag(entt::registry& registry, const ComponentInfo& info)
	{
		for (const entt::entity entity : registry.view<ValueBags>())
		{
			ValueBag* bag = find_bag(registry, entity, info.Name);
			if (bag == nullptr)
			{
				continue;
			}

			std::unordered_map<std::string, FieldValue> values;
			for (const FieldDescriptor& field : info.Fields)
			{
				const auto old = bag->Values.find(field.Name);
				const bool keep = old != bag->Values.end() && is_valid_field_value(field, old->second);
				values[field.Name] = keep ? old->second : field.Default;
			}
			bag->Values = std::move(values);
		}
	}
}
