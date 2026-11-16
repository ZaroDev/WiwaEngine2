#pragma once

#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <nav/nav_agents.h>
#include <physics/physics_snapshot.h>
#include <scene/scene.h>
#include "script_context.h"
#include "script_runtime.h"
#include "anim_handlers.h"
#include "ui_handlers.h"

namespace wiwa
{
	// The scripts running on one scene: a `self` table for every (entity, script) pair and for every scene-level script, and the
	// calls of their hooks. Main thread only.
	//
	// Every update(): instances whose entity or component is gone get on_destroy; new ones get on_init (found by looking at the
	// scene, so entities added by a load or created by a script during the frame start on the next update); then the scene scripts'
	// on_update run, then the entity scripts'. A hook that raises an error is logged and disables that instance only.
	class PhysicsModule;

	class ScriptInstances
	{
	public:
		explicit ScriptInstances(ScriptRuntime& runtime);
		~ScriptInstances();

		ScriptInstances(const ScriptInstances&) = delete;
		ScriptInstances& operator=(const ScriptInstances&) = delete;

		// Begins running scripts on the scene (after stopping on the previous one).
		void start(Ref<Scene> scene);
		// on_destroy of everything that is running; the scene is still the active one while they run.
		void stop();
		void update(float deltaTime);

		// The physics steps the module took since the last frame: for each, the contact and trigger hooks (on_collision_enter, ..., on_trigger_exit) of
		// the scripts on the entities involved, then on_physics_update(self, dt) of every script. Before update(), so a script sees this frame's contacts.
		// While a step is delivered the physics queries see that step.
		void dispatch_physics(PhysicsModule& physics);

		// After ScriptRuntime::reload_script: an instance that an error had disabled starts over (a new self, on_init) with the new code, and
		// a script that could not be loaded is tried again. Instances that were running keep their self and carry on with the new code.
		void script_reloaded(std::span<const AssetRef> scripts);
		// A script that failed to load and has not been reloaded since.
		bool is_broken(AssetGuid script) const { return m_BrokenScripts.contains(script); }

		bool running() const { return m_Scene != nullptr; }
		const Ref<Scene>& scene() const { return m_Scene; }
		ScriptContext& context() { return m_Context; }
		UiHandlerTable& ui_handlers() { return m_UiHandlers; }
		AnimHandlerTable& animation_handlers() { return m_AnimHandlers; }

	private:
		struct Instance
		{
			EntityGuid Entity{}; // invalid for a scene-level script
			AssetGuid Script{};  // the resolved guid of the Script asset
			ScriptClass* Class = nullptr;
			sol::table Self{};
			bool Failed = false;
		};

		ScriptClass* class_of(AssetGuid script);
		bool alive(const Instance& instance) const;
		// Forgets a finished instance: the UI and animation listeners it registered, and its (entity, script) entry.
		void drop_owner(const Instance& instance);
		void reap();
		void dispatch_ui_events();
		void dispatch_animation_events();
		void dispatch_nav_events();
		void discover();
		void make_self(Instance& instance);
		void begin(Instance instance);
		bool call_hook(Instance& instance, const char* hook, const float* deltaTime);
		bool call_hook_with(Instance& instance, const char* hook, const std::function<sol::protected_function_result(const sol::protected_function&)>& invoke);
		void deliver_physics_event(const PhysicsEvent& event);
		std::string describe(const Instance& instance) const;

		ScriptRuntime& m_Runtime;
		ScriptContext m_Context{};
		UiHandlerTable m_UiHandlers{};
		AnimHandlerTable m_AnimHandlers{};
		u32 m_NavListener = 0;                            // the NavModule listener that queues the agents' events (0: none)
		std::vector<std::pair<EntityGuid, NavAgentState>> m_NavEvents{}; // arrivals and failures since the last update, in order
		Ref<Scene> m_Scene{};
		std::vector<Instance> m_Instances{};              // the entity scripts, in the order they were found
		std::vector<Instance> m_SceneInstances{};         // the scene-level scripts
		std::set<std::pair<EntityGuid, AssetGuid>> m_Known{}; // every (entity, script) in m_Instances; scene scripts use an invalid entity
		std::set<AssetGuid> m_BrokenScripts{};            // scripts that failed to load, so that it is logged once
	};
}
