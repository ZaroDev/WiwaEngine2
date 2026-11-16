#pragma once
#include "module.h"

#include <typeindex>
#include <unordered_map>
#include <vector>

namespace wiwa
{
	class PerfStats;

	class ModuleManager
	{
	public:
		ModuleManager() = default;
		~ModuleManager();


		template<typename T, typename... Args>
		T* add_module(Args&&... args)
		{
			T* module = new T(std::forward<Args>(args)...);
			m_Modules[typeid(T)] = module;
			m_CreationOrder.push_back(module);
			module->init();
			return module;
		}


		template<typename T>
		T* get_module()
		{
			return reinterpret_cast<T*>(m_Modules[typeid(T)]);
		}

		// The module calls of each frame are timed into this (not owned; may be null).
		void set_perf_stats(PerfStats* perf) { m_Perf = perf; }

		void begin();

		void clean();

		void pre_update(float deltaTime);
		void update(float deltaTime);
		void post_update(float deltaTime);

		void pre_render_update(float deltaTime);
		void render_update(float deltaTime);
		void post_render_update(float deltaTime);

	private:
		template<typename Call>
		void run_phase(UpdateFlags flag, Call call);

		PerfStats* m_Perf = nullptr;
		std::unordered_map<std::type_index, IModule*> m_Modules{};
		// Modules are shut down and deleted in the reverse order they were added: a module may use the ones added before it.
		std::vector<IModule*> m_CreationOrder{};
		std::vector<IModule*> m_UpdateModules{};
	};
}