#pragma once

#include <string>
#include <vector>

#include <core/common.h>
#include "blackboard.h"
#include "bt_types.h"

namespace wiwa
{
	enum class BtKind : u8
	{
		Sequence, Selector, Parallel,
		Inverter, Succeeder, Repeat, Cooldown, Guard,
		Wait, SetBlackboard, CheckBlackboard, Log,
		External, // MoveTo, PlayAnimation, LuaAction, LuaCondition: done by the services of the instance (bt_instance.h)
	};

	// One node of a compiled tree: the file's node with its parameters read once. Children are indices into CompiledTree::Nodes.
	struct CompiledNode
	{
		BtKind Kind = BtKind::Sequence;
		std::string Id{};
		std::string Type{};
		std::string Name{};
		std::vector<u32> Children{};
		nlohmann::json Params = nlohmann::json::object(); // as the file has it (the External nodes read theirs)

		bool Reactive = false;      // Sequence, Selector
		bool SuccessAll = true;     // Parallel: succeeds when all children succeed (false: when one does)
		bool FailureOne = true;     // Parallel: fails when one child fails (false: when all do)
		i32 Count = 0;              // Repeat: 0 = for ever
		double Seconds = -1.0;      // Wait, Cooldown: -1 = not given
		double Min = -1.0;          // Wait: a random time between Min and Max when Seconds is not given
		double Max = -1.0;
		std::string Key{};          // Guard, SetBlackboard, CheckBlackboard
		std::string Op{};           // Guard, CheckBlackboard
		std::optional<BtValue> Literal{}; // the value of "value", of the type of Key
		std::string Message{};      // Log
	};

	// An immutable tree, shared by every controller that uses the asset.
	struct CompiledTree
	{
		std::vector<CompiledNode> Nodes{};
		u32 Root = 0;
		std::vector<BtKeyDesc> Blackboard{};
	};

	// Null, with the reasons (every error of validate_bt) in error, for a tree that is not valid. Nodes the root does not reach are left out.
	Ref<const CompiledTree> compile_bt(const BehaviorTreeDoc& doc, std::string& error);
}
