#include <pch.h>
#include "component_registry.h"

namespace wiwa
{
	ComponentRegistry& ComponentRegistry::get()
	{
		static ComponentRegistry registry;
		return registry;
	}

	const ComponentInfo* ComponentRegistry::find(std::string_view name) const
	{
		for (const std::unique_ptr<ComponentInfo>& info : m_Infos)
		{
			if (info->Name == name)
			{
				return info.get();
			}
		}
		return nullptr;
	}

	void ComponentRegistry::register_component(ComponentInfo info)
	{
		for (std::unique_ptr<ComponentInfo>& existing : m_Infos)
		{
			if (existing->Name == info.Name)
			{
				*existing = std::move(info);
				rebuild_view();
				return;
			}
		}

		m_Infos.push_back(CreateScope<ComponentInfo>(std::move(info)));
		rebuild_view();
	}

	void ComponentRegistry::unregister_component(std::string_view name)
	{
		std::erase_if(m_Infos, [&](const std::unique_ptr<ComponentInfo>& info) { return info->Name == name; });
		rebuild_view();
	}

	void ComponentRegistry::rebuild_view()
	{
		m_View.clear();
		for (const std::unique_ptr<ComponentInfo>& info : m_Infos)
		{
			m_View.push_back(info.get());
		}
	}
}
