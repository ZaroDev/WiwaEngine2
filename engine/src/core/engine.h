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

		// What main returns; a command-line run (--pack, ...) sets it to 1 when it fails.
		void set_exit_code(int code) { m_ExitCode = code; }
		int get_exit_code() const { return m_ExitCode; }

		template<typename T>
		T* get_module()
		{
			return m_ModuleManager.get_module<T>();
		}

		Time& get_time() { return m_Time; }

		// The command-line arguments after the program name.
		const std::vector<std::string>& get_arguments() const { return m_Arguments; }
	protected:
		virtual void on_init() {};
		virtual void on_update(float deltaTime) {};
		virtual void on_shutdown() {};

		inline static Engine* s_Engine = nullptr;
		
		Time m_Time{};
		ModuleManager m_ModuleManager{};
		std::vector<std::string> m_Arguments{};
		bool m_Running = true;
		int m_ExitCode = 0;
	};

	extern Engine* create_engine(int argc, char** argv);
}