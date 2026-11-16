#include <pch.h>
#include "bt_compile.h"

#include <unordered_map>

#include "bt_file.h"

namespace wiwa
{
	namespace
	{
		BtKind kind_of(const std::string& type)
		{
			if (type == "Sequence") return BtKind::Sequence;
			if (type == "Selector") return BtKind::Selector;
			if (type == "Parallel") return BtKind::Parallel;
			if (type == "Inverter") return BtKind::Inverter;
			if (type == "Succeeder") return BtKind::Succeeder;
			if (type == "Repeat") return BtKind::Repeat;
			if (type == "Cooldown") return BtKind::Cooldown;
			if (type == "Guard") return BtKind::Guard;
			if (type == "Wait") return BtKind::Wait;
			if (type == "SetBlackboard") return BtKind::SetBlackboard;
			if (type == "CheckBlackboard") return BtKind::CheckBlackboard;
			if (type == "Log") return BtKind::Log;
			return BtKind::External;
		}
	}

	Ref<const CompiledTree> compile_bt(const BehaviorTreeDoc& doc, std::string& error)
	{
		const std::vector<BtProblem> problems = validate_bt(doc);
		if (bt_has_errors(problems))
		{
			error.clear();
			for (const BtProblem& problem : problems)
			{
				if (problem.Error)
				{
					error += (error.empty() ? "" : "; ") + (problem.NodeId.empty() ? std::string() : problem.NodeId + ": ") + problem.Message;
				}
			}
			return nullptr;
		}

		auto tree = CreateRef<CompiledTree>();
		tree->Blackboard = doc.Blackboard;

		// Depth first from the root: the order of Nodes is the order the root reaches them in.
		std::unordered_map<std::string, u32> index;
		std::vector<const BtNodeDesc*> order;
		std::vector<const BtNodeDesc*> stack{ doc.find(doc.Root) };
		while (!stack.empty())
		{
			const BtNodeDesc* node = stack.back();
			stack.pop_back();
			index[node->Id] = static_cast<u32>(order.size());
			order.push_back(node);
			for (auto child = node->Children.rbegin(); child != node->Children.rend(); ++child)
			{
				stack.push_back(doc.find(*child));
			}
		}

		for (const BtNodeDesc* node : order)
		{
			CompiledNode out;
			out.Kind = kind_of(node->Type);
			out.Id = node->Id;
			out.Type = node->Type;
			out.Name = node->Name;
			out.Params = node->Params;
			for (const std::string& child : node->Children)
			{
				out.Children.push_back(index[child]);
			}

			const auto number = [&](const char* name, double fallback) { const auto it = node->Params.find(name); return it != node->Params.end() && it->is_number() ? it->get<double>() : fallback; };
			const auto text = [&](const char* name) { const auto it = node->Params.find(name); return it != node->Params.end() && it->is_string() ? it->get<std::string>() : std::string(); };
			out.Reactive = node->Params.contains("reactive") && node->Params["reactive"].is_boolean() && node->Params["reactive"].get<bool>();
			out.SuccessAll = text("success") != "one";
			out.FailureOne = text("failure") != "all";
			out.Count = static_cast<i32>(number("count", 0.0));
			out.Seconds = number("seconds", -1.0);
			out.Min = number("min", -1.0);
			out.Max = number("max", -1.0);
			out.Key = text("key");
			out.Op = text("op");
			out.Message = text("message");
			if (const BtKeyDesc* key = doc.find_key(out.Key); key != nullptr && node->Params.contains("value"))
			{
				out.Literal = bt_value_from_json(key->Type, node->Params["value"]);
			}
			else if (key != nullptr && key->Type == BtValueType::Entity)
			{
				out.Literal = BtValue(EntityGuid{}); // an entity key without a value means none
			}
			tree->Nodes.push_back(std::move(out));
		}
		tree->Root = 0;
		return tree;
	}
}
