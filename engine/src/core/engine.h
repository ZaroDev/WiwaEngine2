#pragma once

#include <core/module_manager.h>

#include <time/time.h>


namespace wiwa {
	class Engine
	{
	public:
		Engine(int argc, char** argv);
		virtual ~Engine();

		void init();

		void update();

		void shutdown();

		static Engine& get() { return *s_Engine; }

		void request_quit();

		template<typename T>
		T* get_module()
		{
			return m_ModuleManager.get_module<T>();
		}

		Time& get_time() { return m_Time; }
	protected:
		virtual void on_init() {};
		virtual void on_update(float deltaTime) {};
		virtual void on_shutdown() {};

		inline static Engine* s_Engine = nullptr;
		
		Time m_Time{};
		ModuleManager m_ModuleManager{};
		bool m_Running = true;
	};

	extern Engine* create_engine(int argc, char** argv);
}