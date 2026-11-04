#pragma once
#include <core/module.h>

#include <functional>
#include <mutex>
#include <vector>

#include "asset_script_source.h"
#include "script_instances.h"
#include "script_runtime.h"

namespace wiwa
{
	// Runs Lua scripts: loads the Script assets, and while a scene is active runs the scripts on it (see ScriptInstances). In a game
	// the lifecycle starts with the first scene; the editor turns that off (set_auto_start) and starts and stops it for play mode.
	class ScriptingModule : public IModule
	{
	public:
		ScriptingModule() = default;
		virtual ~ScriptingModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "ScriptingModule"; }
		virtual int get_priority() const override { return 0; }

		virtual void update(float deltaTime) override;
		virtual void post_update(float deltaTime) override;

		// With auto start (the default) scripts run on whatever scene is active, and follow it when it is replaced.
		void set_auto_start(bool autoStart) { m_AutoStart = autoStart; }

		// Runs the scripts on the active scene until stop(). For play mode; does nothing if they already run.
		void start();
		// on_destroy of every script; the scripts stay stopped (an auto-start module starts again with the next update).
		void stop();
		bool running() const { return m_Instances != nullptr && m_Instances->running(); }

		// What quit() in a script does. A game ends the program (the default); the editor replaces it with stopping play mode.
		void set_quit_handler(std::function<void()> handler) { m_Instances->context().RequestQuit = std::move(handler); }

		ScriptRuntime* get_runtime() { return m_Runtime.get(); }

	private:
		void register_catalog_scripts();
		void process_reloads();
		void follow_active_scene();

		Scope<AssetScriptSource> m_Source{};
		Scope<ScriptRuntime> m_Runtime{};
		Scope<ScriptInstances> m_Instances{};
		bool m_AutoStart = true;

		// Scripts that were cooked again, noted on the importer's thread and reloaded on the main one.
		u32 m_ReloadListener = 0;
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadQueue{};
	};
}
