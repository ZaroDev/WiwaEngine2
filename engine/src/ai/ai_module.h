#pragma once

#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

#include <core/common.h>
#include <core/module.h>

#include <assets/asset_manager.h>
#include <assets/handle.h>

#include <vector>

#include "bt_instance.h"
#include "bt_leaves_engine.h"
#include "perception.h"
#include "bt_lua_host.h"

namespace wiwa
{
	class NavModule;
	class Scene;
	struct AIController;

	// Runs the behaviour trees of the scene that plays: one BtInstance per enabled AIController, ticked at the controller's rate. Main thread only; the
	// render and physics threads never touch it. Priority 6: after the scripts (0), so what they put on a blackboard is read this frame, and after the
	// NavModule (5), so MoveTo sees the state of this frame's crowd step; before the animation (10).
	//
	// A world exists while a scene plays: with auto start (the default) the module follows the SceneModule's active scene while the game clock plays and
	// ends it when another replaces it (Editor Play runs a clone; Stop discards it, and the controllers' running leaves are aborted once). Without auto
	// start (checks) start_world / stop_world drive it.
	class AIModule final : public IModule
	{
	public:
		AIModule() = default;
		virtual ~AIModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "AIModule"; }
		virtual int get_priority() const override { return 6; }

		void set_auto_start(bool autoStart) { m_AutoStart = autoStart; }
		// The navigation module MoveTo uses (init() takes the engine's; a check gives its own).
		void set_nav(NavModule* nav) { m_Hooks.Nav = nav; }

		bool start_world(Ref<Scene> scene);
		// Aborts what runs (once) and forgets the controllers. Does nothing when no world runs.
		void stop_world();
		Ref<Scene> world_scene() const { return m_HasWorld ? m_World.lock() : nullptr; }

		// One frame for the given active scene and clock state; dt is the game clock's seconds. update() calls it with the engine's; a check can call it
		// without an Engine.
		void tick(const Ref<Scene>& active, bool playing, float dt);

		// The tree of an AIController is a BehaviorTree asset; for checks and tools a tree can be given by hand under a reference. A changed tree compiles
		// again and the controllers that use it restart. False, with why in error, if it does not compile.
		bool register_tree(const AssetRef& ref, const BehaviorTreeDoc& doc, std::string* error = nullptr);

		// The instance of a controller of the world's scene (null when it has none yet: no tree, loading, disabled).
		// For scripts, which run before tick(): the instance of an enabled controller of the world's scene, made now if there is none yet (so a script
		// can use the blackboard in the frame the scene starts). Null when the entity has no enabled controller, its tree is unset, still loading or
		// invalid, or no world runs.
		BtInstance* acquire(entt::entity entity);
		// Aborts what the controller runs and puts its blackboard back to the declared defaults and overrides. False without an instance.
		bool restart(entt::entity entity);

		// Sounds the scripts made this frame (AI.emit_noise), for perception; cleared at the end of every tick.
		void emit_noise(glm::vec3 position, float loudness, std::string kind);
		const std::vector<Noise>& noises() const { return m_Noises; }

		// Sight rays: init() uses the physics query snapshot; a check gives its own. Without any, nothing blocks sight.
		void set_sight_raycast(SightRaycast raycast) { m_SightRaycast = std::move(raycast); }
		const Perception& perception() const { return m_Perception; }

		BtInstance* instance_of(entt::entity entity);
		const BtInstance* instance_of(entt::entity entity) const;
		u32 active_count() const;
		// Game seconds the module has ticked for in this world.
		double time() const { return m_Time; }

		// The Lua leaves (LuaAction / LuaCondition nodes) run on the scripting runtime: the ScriptingModule attaches it when it is made and detaches it
		// before the runtime goes. Detaching aborts every running tree first, while the Lua state is still there.
		void attach_lua(ScriptRuntime& runtime, ScriptContext& context);
		void detach_lua();
		BtLuaHost* lua_host() { return m_Lua.get(); }

		// What a External leaf does besides the built-in ones (the Lua leaves, set by the Lua host).
		void set_leaf_handler(std::function<BtStatus(BtLeafCall&)> handler) { m_LeafHandler = std::move(handler); }
		BtServices& services() { return m_Services; }

	private:
		struct TreeEntry
		{
			Ref<const CompiledTree> Tree{};
			Handle<BehaviorTreeData> Handle{};
			bool Loading = false;
			bool Failed = false;
			bool Manual = false;      // given by register_tree: never loaded from an asset
			std::string Error{};
		};

		struct Controller
		{
			Scope<BtInstance> Instance{};
			Ref<const CompiledTree> Tree{};
			double LastTick = 0.0;
			double NextTick = 0.0;
			bool Warned = false;
			bool Seen = false;
		};

		TreeEntry& entry_for(const AssetRef& ref);
		void refresh_entry(const AssetRef& ref, TreeEntry& entry);
		void drop(Controller& controller);
		void tick_controllers(Scene& scene, float dt);
		BtInstance* ensure_instance(entt::entity entity, const AIController& ai, Controller& controller, float dt);
		BtStatus leaf(BtLeafCall& call);

		bool m_AutoStart = true;
		bool m_HasWorld = false;
		std::weak_ptr<Scene> m_World{};
		double m_Time = 0.0;
		BtEngineHooks m_Hooks{};
		BtServices m_Services{};
		std::function<BtStatus(BtLeafCall&)> m_LeafHandler{};
		Scope<BtLuaHost> m_Lua{};
		std::unordered_map<entt::entity, Controller> m_Controllers{};
		std::vector<Noise> m_Noises{};
		Perception m_Perception{};
		SightRaycast m_SightRaycast{};
		std::unordered_map<AssetGuid, TreeEntry> m_Trees{};

		std::mutex m_DirtyMutex{};
		std::set<AssetGuid> m_Dirty{};   // trees whose asset was reimported (the asset manager tells from any thread)
		u32 m_ReloadListener = 0;
	};
}
