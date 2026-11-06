#pragma once

#include <core/module_manager.h>
#include <core/perf_stats.h>
#include <core/project_settings.h>

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

		// Timings and memory for the Performance panel. Main thread only.
		PerfStats& get_perf() { return m_Perf; }

		// The command-line arguments after the program name.
		const std::vector<std::string>& get_arguments() const { return m_Arguments; }

		// The project's name, version and launch settings: from the pack of a game, from project.wproject in the editor's working directory.
		const ProjectSettings& project() const { return m_Project; }
		void set_project(ProjectSettings project) { m_Project = sanitized(std::move(project)); }
		// True when they came from the pack: only then does a game take its window and launch values from them (the editor owns its window).
		bool project_from_pak() const { return m_ProjectFromPak; }
	protected:
		virtual void on_init() {};
		virtual void on_update(float deltaTime) {};
		virtual void on_shutdown() {};

		// Reads the project settings: the pack's if the asset source has them, else project.wproject (defaults, logged once, if neither).
		void load_project(class AssetManager& assets);

		inline static Engine* s_Engine = nullptr;
		
		Time m_Time{};
		PerfStats m_Perf{};
		ModuleManager m_ModuleManager{};
		std::vector<std::string> m_Arguments{};
		ProjectSettings m_Project{};
		bool m_ProjectFromPak = false;
		bool m_Running = true;
		int m_ExitCode = 0;
	};

	extern Engine* create_engine(int argc, char** argv);
}