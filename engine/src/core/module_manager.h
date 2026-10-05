#pragma once
#include "module.h"

#include <typeindex>
#include <unordered_map>
#include <vector>

namespace wiwa
{
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
			module->init();
			return module;
		}


		template<typename T>
		T* get_module()
		{
			return reinterpret_cast<T*>(m_Modules[typeid(T)]);
		}

		void begin();

		void clean();

		void pre_update(float deltaTime);
		void update(float deltaTime);
		void post_update(float deltaTime);

		void pre_physics_update(float deltaTime);
		void physics_update(float deltaTime);
		void post_physics_update(float deltaTime);

		void pre_render_update(float deltaTime);
		void render_update(float deltaTime);
		void post_render_update(float deltaTime);

	private:
		std::unordered_map<std::type_index, IModule*> m_Modules{};
		std::vector<IModule*> m_UpdateModules{};
	};
}