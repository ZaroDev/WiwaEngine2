#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "component_info.h"

namespace wiwa
{
	// Every component the scene knows by name. Main thread only: components are registered at start-up, and scripts add
	// theirs while they load.
	class ComponentRegistry
	{
	public:
		static ComponentRegistry& get();

		const ComponentInfo* find(std::string_view name) const;
		const std::vector<const ComponentInfo*>& all() const { return m_View; }

		// A component with the same name is replaced, and pointers to the old one are no longer valid.
		void register_component(ComponentInfo info);
		void unregister_component(std::string_view name);

	private:
		void rebuild_view();

		std::vector<std::unique_ptr<ComponentInfo>> m_Infos{};
		std::vector<const ComponentInfo*> m_View{};
	};
}
