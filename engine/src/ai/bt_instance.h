#pragma once

#include <functional>
#include <random>
#include <vector>

#include <entt/entt.hpp>

#include <core/common.h>
#include "blackboard.h"
#include "bt_compile.h"

namespace wiwa
{
	class Scene;
	class BtInstance;

	enum class BtStatus : u8 { Success, Failure, Running };

	// What an External node (MoveTo, PlayAnimation, LuaAction, LuaCondition) is asked to do: start (the first call of a run; its answer is the first
	// tick's), tick (every call after, while it said Running), abort (the branch it is in was cut: it must stop what it started; the answer is ignored).
	enum class BtLeafPhase : u8 { Start, Tick, Abort };

	struct BtLeafCall
	{
		BtLeafPhase Phase = BtLeafPhase::Start;
		const CompiledNode* Node = nullptr;
		u32 NodeIndex = 0;
		BtInstance* Instance = nullptr;
		entt::entity Self = entt::null;
		float Dt = 0.0f;
		double Time = 0.0;
	};

	// What an instance needs from the rest of the engine, so that the tree logic runs (and is checked) without it. Set by the AIModule.
	struct BtServices
	{
		std::function<BtStatus(BtLeafCall&)> Leaf{};
	};

	// What a node did at its last tick, for tools (the editor shows it in play later).
	struct BtNodeState
	{
		bool Running = false;
		i32 Active = -1;              // Sequence, Selector: the child that said Running
		i32 Count = 0;                // Repeat
		double Stamp = -1.0e300;      // Cooldown: when it last finished
		double Until = 0.0;           // Wait: when it ends
		std::vector<u8> ChildStatus{}; // Parallel: 0 pending, 1 succeeded, 2 failed
		BtStatus Last = BtStatus::Failure;
		bool Warned = false;
		glm::vec3 Anchor{ 0.0f };     // for External leaves: where the leaf last sent the entity
	};

	// The run of one tree for one entity: the state of every node, the blackboard, a random generator. The compiled tree is shared and never changes.
	class BtInstance
	{
	public:
		BtInstance(Ref<const CompiledTree> tree, entt::entity self, u32 seed = 0);

		// One tick of the whole tree from the root. Nodes that said Running continue where they were.
		BtStatus tick(BtServices& services, float dt, double time);
		// Cuts the running branch: every running External leaf gets its abort, once. The state is reset; the blackboard is kept.
		void abort(BtServices& services, double time);
		// Abort, then the blackboard back to its declared defaults.
		void restart(BtServices& services, double time);

		bool running() const { return m_States[m_Tree->Root].Running; }
		Blackboard& blackboard() { return m_Blackboard; }
		const Blackboard& blackboard() const { return m_Blackboard; }
		const CompiledTree& tree() const { return *m_Tree; }
		Ref<const CompiledTree> tree_ref() const { return m_Tree; }
		const std::vector<BtNodeState>& states() const { return m_States; }
		BtNodeState& state(u32 index) { return m_States[index]; }
		// True the first time it is asked for this node in a run (a run ends with a cut or a finish... the flag is reset when the node is cut); for logging a failure once.
		bool warn_once(u32 index)
		{
			if (m_States[index].Warned)
			{
				return false;
			}
			m_States[index].Warned = true;
			return true;
		}
		entt::entity self() const { return m_Self; }
		// The node whose MoveTo last sent the entity somewhere (NO_OWNER: none). A branch that is cut after another one has started its own walk (a reactive
		// selector aborts the old branch after the new one has run) must not stop that walk: only the owner's abort does.
		static constexpr u32 NO_OWNER = 0xFFFFFFFFu;
		u32 move_owner() const { return m_MoveOwner; }
		void set_move_owner(u32 node) { m_MoveOwner = node; }
		// A random number in [low, high] from the instance's generator (Wait, Lua leaves that ask).
		double random(double low, double high);

	private:
		BtStatus run(u32 index);
		void cut(u32 index);
		BtStatus run_external(u32 index, const CompiledNode& node, BtNodeState& state);
		bool condition(const CompiledNode& node) const;
		BtStatus finish(u32 index, BtStatus status);

		Ref<const CompiledTree> m_Tree{};
		entt::entity m_Self{ entt::null };
		Blackboard m_Blackboard{};
		std::vector<BtNodeState> m_States{};
		std::mt19937 m_Random{};
		BtServices* m_Services = nullptr;
		float m_Dt = 0.0f;
		double m_Time = 0.0;
		u32 m_MoveOwner = NO_OWNER;
	};
}
