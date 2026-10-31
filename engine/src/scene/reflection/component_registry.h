#pragma once

#include <atomic>
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
		// A snapshot: the list never changes after it is handed out and is never freed, so it is safe to hold and to use from any thread.
		const std::vector<const ComponentInfo*>& all() const { return *m_Current.load(std::memory_order_acquire); }
		std::optional<ComponentSchema> schema(std::string_view name) const;

		// A component with the same name is replaced. Pointers to the old one stay valid (it is kept, unchanged, for good), so a reader on
		// another thread that holds one is never left with freed memory; it simply sees the old description.
		void register_component(ComponentInfo info);
		void unregister_component(std::string_view name);

	private:
		void rebuild_view();

		mutable std::shared_mutex m_Mutex{};
		std::vector<std::unique_ptr<ComponentInfo>> m_Infos{};
		std::vector<std::unique_ptr<ComponentInfo>> m_Retired{};                       // replaced or removed: kept so that held pointers stay valid
		std::vector<std::unique_ptr<std::vector<const ComponentInfo*>>> m_Views{};     // every snapshot ever published
		std::vector<const ComponentInfo*> m_Empty{};
		std::atomic<const std::vector<const ComponentInfo*>*> m_Current{ &m_Empty };
	};
}
