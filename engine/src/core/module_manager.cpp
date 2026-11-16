#include <pch.h>
#include "module_manager.h"

#include <algorithm>
#include <chrono>

#include "perf_stats.h"

namespace wiwa
{
	ModuleManager::~ModuleManager()
	{
		clean();
	}

	void ModuleManager::clean()
	{
		for (auto it = m_CreationOrder.rbegin(); it != m_CreationOrder.rend(); ++it)
		{
			(*it)->shutdown();
		}

		for (auto it = m_CreationOrder.rbegin(); it != m_CreationOrder.rend(); ++it)
		{
			delete *it;
		}

		m_CreationOrder.clear();
		m_UpdateModules.clear();
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

	// Calls one phase of every module that has the flag, in priority order, and adds each call's time to the frame's module times.
	template<typename Call>
	void ModuleManager::run_phase(UpdateFlags flag, Call call)
	{
		for (IModule* module : m_UpdateModules)
		{
			if (!any(module->get_update_flags(), flag))
			{
				continue;
			}

			const auto start = std::chrono::steady_clock::now();
			call(module);
			if (m_Perf != nullptr)
			{
				m_Perf->add_module_ms(module->get_name(), std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count());
			}
		}
	}

	void ModuleManager::pre_update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE, [deltaTime](IModule* module) { module->pre_update(deltaTime); });
	}

	void ModuleManager::update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE, [deltaTime](IModule* module) { module->update(deltaTime); });
	}

	void ModuleManager::post_update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE, [deltaTime](IModule* module) { module->post_update(deltaTime); });
	}

	void ModuleManager::pre_render_update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE_RENDERER, [deltaTime](IModule* module) { module->pre_render_update(deltaTime); });
	}

	void ModuleManager::render_update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE_RENDERER, [deltaTime](IModule* module) { module->render_update(deltaTime); });
	}

	void ModuleManager::post_render_update(float deltaTime)
	{
		run_phase(UpdateFlags::UPDATE_RENDERER, [deltaTime](IModule* module) { module->post_render_update(deltaTime); });
	}
}
