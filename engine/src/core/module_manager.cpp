#include <pch.h>
#include "module_manager.h"

#include <algorithm>

namespace wiwa
{
	ModuleManager::~ModuleManager()
	{
		clean();
	}

	void ModuleManager::clean()
	{
		for (auto [_, module] : m_Modules)
		{
			module->shutdown();
		}

		for (auto [_, module] : m_Modules)
		{
			delete module;
		}

		m_Modules.clear();
	}


	void ModuleManager::begin()
	{
		m_UpdateModules.clear();

		for (auto [_, module] : m_Modules)
		{
			m_UpdateModules.push_back(module);
		}

		std::sort(m_UpdateModules.begin(), m_UpdateModules.end(), [](const auto& a, const auto& b)
			{
				return a->get_priority() < b->get_priority();
			});
	}

	void ModuleManager::pre_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE))
			{
				module->pre_update(deltaTime);
			}
		}
	}

	void ModuleManager::update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE))
			{
				module->update(deltaTime);
			}
		}
	}

	void ModuleManager::post_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE))
			{
				module->post_update(deltaTime);
			}
		}
	}


	void ModuleManager::pre_physics_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_PHYSICS))
			{
				module->pre_physics_update(deltaTime);
			}
		}
	}

	void ModuleManager::physics_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_PHYSICS))
			{
				module->physics_update(deltaTime);
			}
		}
	}

	void ModuleManager::post_physics_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_PHYSICS))
			{
				module->post_physics_update(deltaTime);
			}
		}
	}

	void ModuleManager::pre_render_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_RENDERER))
			{
				module->pre_render_update(deltaTime);
			}
		}
	}

	void ModuleManager::render_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_RENDERER))
			{
				module->render_update(deltaTime);
			}
		}
	}

	void ModuleManager::post_render_update(float deltaTime)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (any(module->get_update_flags(), UpdateFlags::UPDATE_RENDERER))
			{
				module->post_render_update(deltaTime);
			}
		}
	}
}