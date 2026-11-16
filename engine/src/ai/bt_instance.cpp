#include <pch.h>
#include "bt_instance.h"

namespace wiwa
{
	namespace
	{
		bool compare(double a, double b, const std::string& op)
		{
			if (op == "==") return a == b;
			if (op == "!=") return a != b;
			if (op == "<") return a < b;
			if (op == "<=") return a <= b;
			if (op == ">") return a > b;
			return a >= b;
		}

		double as_number(const BtValue& value)
		{
			return std::holds_alternative<i32>(value) ? static_cast<double>(std::get<i32>(value)) : static_cast<double>(std::get<float>(value));
		}
	}

	BtInstance::BtInstance(Ref<const CompiledTree> tree, entt::entity self, u32 seed)
		: m_Tree(std::move(tree)), m_Self(self), m_States(m_Tree->Nodes.size()), m_Random(seed ^ (static_cast<u32>(entt::to_integral(self)) * 2654435761u))
	{
		m_Blackboard.declare(m_Tree->Blackboard);
	}

	double BtInstance::random(double low, double high)
	{
		if (!(high > low))
		{
			return low;
		}
		return std::uniform_real_distribution<double>(low, high)(m_Random);
	}

	BtStatus BtInstance::tick(BtServices& services, float dt, double time)
	{
		m_Services = &services;
		m_Dt = dt;
		m_Time = time;
		return run(m_Tree->Root);
	}

	void BtInstance::abort(BtServices& services, double time)
	{
		m_Services = &services;
		m_Time = time;
		cut(m_Tree->Root);
	}

	void BtInstance::restart(BtServices& services, double time)
	{
		abort(services, time);
		m_States.assign(m_Tree->Nodes.size(), BtNodeState{});
		m_Blackboard.declare(m_Tree->Blackboard);
	}

	BtStatus BtInstance::finish(u32 index, BtStatus status)
	{
		BtNodeState& state = m_States[index];
		state.Running = status == BtStatus::Running;
		state.Last = status;
		return status;
	}

	bool BtInstance::condition(const CompiledNode& node) const
	{
		const std::optional<BtValue> value = m_Blackboard.get(node.Key);
		if (!value)
		{
			return false;
		}
		if (node.Op == "true" || node.Op == "false")
		{
			return std::holds_alternative<bool>(*value) && std::get<bool>(*value) == (node.Op == "true");
		}
		if (!node.Literal || bt_value_type_of(*value) != bt_value_type_of(*node.Literal))
		{
			return false;
		}
		switch (bt_value_type_of(*value))
		{
		case BtValueType::Bool:
		case BtValueType::Vec3:
		case BtValueType::String:
		case BtValueType::Entity:
			return node.Op == "==" ? *value == *node.Literal : *value != *node.Literal;
		case BtValueType::Int:
		case BtValueType::Float:
			return compare(as_number(*value), as_number(*node.Literal), node.Op);
		}
		return false;
	}

	BtStatus BtInstance::run_external(u32 index, const CompiledNode& node, BtNodeState& state)
	{
		if (!m_Services || !m_Services->Leaf)
		{
			if (!state.Warned)
			{
				state.Warned = true;
				WI_AI_LOG(Warning, "Behaviour tree: {0} node {1} has nothing to run it", node.Type, node.Id);
			}
			return BtStatus::Failure;
		}
		BtLeafCall call;
		call.Phase = state.Running ? BtLeafPhase::Tick : BtLeafPhase::Start;
		call.Node = &node;
		call.NodeIndex = index;
		call.Instance = this;
		call.Self = m_Self;
		call.Dt = m_Dt;
		call.Time = m_Time;
		return m_Services->Leaf(call);
	}

	BtStatus BtInstance::run(u32 index)
	{
		const CompiledNode& node = m_Tree->Nodes[index];
		BtNodeState& state = m_States[index];
		const size_t count = node.Children.size();

		switch (node.Kind)
		{
		case BtKind::Sequence:
		case BtKind::Selector:
		{
			const bool sequence = node.Kind == BtKind::Sequence;
			const BtStatus stops = sequence ? BtStatus::Failure : BtStatus::Success; // what ends the node at once
			const i32 previous = state.Active;
			const size_t first = node.Reactive ? 0 : static_cast<size_t>(std::max(previous, 0));
			for (size_t i = first; i < count; i++)
			{
				const BtStatus status = run(node.Children[i]);
				if (status == BtStatus::Running || status == stops)
				{
					if (previous >= 0 && previous != static_cast<i32>(i))
					{
						cut(node.Children[static_cast<size_t>(previous)]); // a child before the running one decided: the running one is cut
					}
					state.Active = status == BtStatus::Running ? static_cast<i32>(i) : -1;
					return finish(index, status);
				}
			}
			state.Active = -1;
			return finish(index, sequence ? BtStatus::Success : BtStatus::Failure);
		}

		case BtKind::Parallel:
		{
			if (!state.Running || state.ChildStatus.size() != count)
			{
				state.ChildStatus.assign(count, 0);
			}
			for (size_t i = 0; i < count; i++)
			{
				if (state.ChildStatus[i] != 0)
				{
					continue;
				}
				const BtStatus status = run(node.Children[i]);
				state.ChildStatus[i] = status == BtStatus::Success ? 1 : (status == BtStatus::Failure ? 2 : 0);
			}
			size_t succeeded = 0;
			size_t failed = 0;
			for (const u8 child : state.ChildStatus)
			{
				succeeded += child == 1 ? 1 : 0;
				failed += child == 2 ? 1 : 0;
			}
			const bool failureDone = node.FailureOne ? failed >= 1 : failed == count;
			const bool successDone = node.SuccessAll ? succeeded == count : succeeded >= 1;
			const bool allFinished = succeeded + failed == count;
			if (failureDone || successDone || allFinished)
			{
				for (size_t i = 0; i < count; i++)
				{
					if (state.ChildStatus[i] == 0)
					{
						cut(node.Children[i]);
					}
				}
				state.ChildStatus.clear();
				return finish(index, failureDone ? BtStatus::Failure : (successDone || succeeded > 0 ? BtStatus::Success : BtStatus::Failure));
			}
			return finish(index, BtStatus::Running);
		}

		case BtKind::Inverter:
		{
			const BtStatus status = run(node.Children[0]);
			return finish(index, status == BtStatus::Running ? status : (status == BtStatus::Success ? BtStatus::Failure : BtStatus::Success));
		}

		case BtKind::Succeeder:
		{
			const BtStatus status = run(node.Children[0]);
			return finish(index, status == BtStatus::Running ? status : BtStatus::Success);
		}

		case BtKind::Repeat:
		{
			const BtStatus status = run(node.Children[0]);
			if (status == BtStatus::Running)
			{
				return finish(index, status);
			}
			if (status == BtStatus::Failure)
			{
				state.Count = 0;
				return finish(index, status);
			}
			state.Count++;
			if (node.Count > 0 && state.Count >= node.Count)
			{
				state.Count = 0;
				return finish(index, BtStatus::Success);
			}
			return finish(index, BtStatus::Running);
		}

		case BtKind::Cooldown:
		{
			if (!state.Running && m_Time - state.Stamp < std::max(node.Seconds, 0.0))
			{
				return finish(index, BtStatus::Failure);
			}
			const BtStatus status = run(node.Children[0]);
			if (status != BtStatus::Running)
			{
				state.Stamp = m_Time;
			}
			return finish(index, status);
		}

		case BtKind::Guard:
		{
			if (!condition(node))
			{
				cut(node.Children[0]);
				return finish(index, BtStatus::Failure);
			}
			return finish(index, run(node.Children[0]));
		}

		case BtKind::Wait:
		{
			if (!state.Running)
			{
				const double duration = node.Seconds >= 0.0 ? node.Seconds : (node.Max > node.Min && node.Min >= 0.0 ? random(node.Min, node.Max) : std::max(node.Min, 0.0));
				state.Until = m_Time + duration;
			}
			return finish(index, m_Time >= state.Until ? BtStatus::Success : BtStatus::Running);
		}

		case BtKind::SetBlackboard:
		{
			std::string error;
			if (!node.Literal || !m_Blackboard.set(node.Key, *node.Literal, &error))
			{
				if (!state.Warned)
				{
					state.Warned = true;
					WI_AI_LOG(Warning, "Behaviour tree: SetBlackboard {0} failed: {1}", node.Id, error);
				}
				return finish(index, BtStatus::Failure);
			}
			return finish(index, BtStatus::Success);
		}

		case BtKind::CheckBlackboard:
			return finish(index, condition(node) ? BtStatus::Success : BtStatus::Failure);

		case BtKind::Log:
			WI_AI_LOG(Info, "{0}", node.Message);
			return finish(index, BtStatus::Success);

		case BtKind::External:
			return finish(index, run_external(index, node, state));
		}
		return finish(index, BtStatus::Failure);
	}

	// Cuts a node that may be running: its running children and External leaves are told, and its state is reset.
	void BtInstance::cut(u32 index)
	{
		const CompiledNode& node = m_Tree->Nodes[index];
		BtNodeState& state = m_States[index];
		if (!state.Running)
		{
			return;
		}

		switch (node.Kind)
		{
		case BtKind::Sequence:
		case BtKind::Selector:
			if (state.Active >= 0)
			{
				cut(node.Children[static_cast<size_t>(state.Active)]);
			}
			break;
		case BtKind::Parallel:
			for (size_t i = 0; i < node.Children.size() && i < state.ChildStatus.size(); i++)
			{
				if (state.ChildStatus[i] == 0)
				{
					cut(node.Children[i]);
				}
			}
			break;
		case BtKind::Inverter:
		case BtKind::Succeeder:
		case BtKind::Repeat:
		case BtKind::Cooldown:
		case BtKind::Guard:
			cut(node.Children[0]);
			break;
		case BtKind::External:
			if (m_Services && m_Services->Leaf)
			{
				BtLeafCall call;
				call.Phase = BtLeafPhase::Abort;
				call.Node = &node;
				call.NodeIndex = index;
				call.Instance = this;
				call.Self = m_Self;
				call.Dt = m_Dt;
				call.Time = m_Time;
				m_Services->Leaf(call);
			}
			break;
		default:
			break;
		}

		const double stamp = state.Stamp;
		state = BtNodeState{};
		state.Stamp = stamp;
	}
}
