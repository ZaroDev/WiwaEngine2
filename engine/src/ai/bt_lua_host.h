#pragma once

#include <map>
#include <string>
#include <utility>

#include <sol/sol.hpp>

#include "bt_instance.h"

namespace wiwa
{
	class ScriptRuntime;
	struct ScriptContext;

	// Defines the Lua base classes of behaviour tree leaves in the runtime's sandbox: BtAction and BtCondition, both made with Script:extend, marked with
	// __bt_kind ("action" / "condition"). A leaf script returns BtAction:extend{ properties = {...}, on_start, on_tick, on_abort } (or BtCondition). Safe to
	// call twice. The ScriptingModule calls it before it loads the scripts of the catalog, which may extend them.
	void define_bt_lua_bases(ScriptRuntime& runtime);
	// A blackboard value as Lua holds it (numbers, vec3, string, an Entity handle or nil for the null entity) and back. The way back refuses what the key's
	// type cannot hold (no conversions): false, with what was expected ("a whole number", "a vec3"...) in why.
	sol::object bt_value_to_lua(sol::state_view lua, ScriptContext* context, const BtValue& value);
	bool bt_value_from_lua(BtValueType type, const sol::object& value, BtValue& out, std::string& why);

	// Whether a loaded script class is a leaf (it inherits from BtAction or BtCondition): "action", "condition", or "" for any other script.
	std::string bt_leaf_kind(const sol::table& scriptClass);

	// Runs the Lua leaves (LuaAction, LuaCondition nodes) of the trees on the scripting runtime. A leaf is not an entity script: each run of a node makes a
	// `self` table of its own (the class by metatable, `self.entity` the AI entity, the class properties with the node's params over their defaults) and
	// calls on_start(self, bb) then on_tick(self, bb, dt) in the same call, on_tick each call after while it says "running", on_abort(self, bb) when the
	// branch is cut. on_tick returns "success", "failure" or "running"; anything else, a hook that raises an error, a script that cannot load or is not a
	// leaf of the right kind makes the node fail, with one log per node run. `bb` is the controller's blackboard (get/set, typed, errors name the key).
	// Main thread only.
	class BtLuaHost
	{
	public:
		BtLuaHost(ScriptRuntime& runtime, ScriptContext& context);

		// The External leaf call for a LuaAction / LuaCondition node.
		BtStatus handle(BtLeafCall& call);

		// Leaves that run now (started, not finished or aborted).
		size_t running() const { return m_Leaves.size(); }

	private:
		struct Leaf
		{
			sol::table Self{};
			sol::table Class{};
			std::string Name{};
		};
		using Key = std::pair<const BtInstance*, u32>;

		BtStatus start(BtLeafCall& call);
		BtStatus tick(BtLeafCall& call, Leaf& leaf);
		void abort(BtLeafCall& call, Leaf& leaf);
		bool fail(BtLeafCall& call, const std::string& script, const std::string& why);
		sol::object blackboard_for(BtInstance& instance);

		ScriptRuntime& m_Runtime;
		ScriptContext& m_Context;
		std::map<Key, Leaf> m_Leaves{};
	};
}
