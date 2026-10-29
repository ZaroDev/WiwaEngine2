#pragma once

#include <memory>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "component_info.h"

namespace wiwa
{
	// The fields of a component, copied out of the registry: safe to keep and to use on any thread.
	struct ComponentSchema
	{
		std::string Name{};
		std::vector<FieldDescriptor> Fields{};

		const FieldDescriptor* find_field(std::string_view name) const
		{
			for (const FieldDescriptor& field : Fields)
			{
				if (field.Name == name)
				{
					return &field;
				}
			}
			return nullptr;
		}
	};

	// Every component the scene knows by name. Components are registered at start-up, and scripts add theirs while they load.
	// find() and all() hand out pointers and are for the main thread only; schema() is for any thread (importers use it).
	class ComponentRegistry
	{
	public:
		static ComponentRegistry& get();

		const ComponentInfo* find(std::string_view name) const;
		const std::vector<const ComponentInfo*>& all() const { return m_View; }
		std::optional<ComponentSchema> schema(std::string_view name) const;

		// A component with the same name is replaced, and pointers to the old one are no longer valid.
		void register_component(ComponentInfo info);
		void unregister_component(std::string_view name);

	private:
		void rebuild_view();

		mutable std::shared_mutex m_Mutex{};
		std::vector<std::unique_ptr<ComponentInfo>> m_Infos{};
		std::vector<const ComponentInfo*> m_View{};
	};
}
