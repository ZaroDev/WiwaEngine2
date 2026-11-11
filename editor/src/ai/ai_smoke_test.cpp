#include <editor_common.h>
#include "ai_smoke_test.h"

#include <algorithm>

#include <ai/bt_file.h>
#include <assets/wasset_io.h>

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_AI_LOG(Error, "AI smoke test FAILED: {0}", what);
				}
			}
		};

		using Json = nlohmann::json;

		BtNodeDesc node(const std::string& id, const std::string& type, std::vector<std::string> children = {}, Json params = Json::object())
		{
			BtNodeDesc desc;
			desc.Id = id;
			desc.Type = type;
			desc.Children = std::move(children);
			desc.Params = std::move(params);
			return desc;
		}

		BtKeyDesc key(const std::string& name, BtValueType type, Json fallback = Json())
		{
			BtKeyDesc desc;
			desc.Key = name;
			desc.Type = type;
			desc.Default = std::move(fallback);
			return desc;
		}

		// A tree that uses most of the node types: patrol, chase what is seen, wait.
		BehaviorTreeDoc example_tree()
		{
			BehaviorTreeDoc doc;
			doc.Root = "root";
			doc.Blackboard = { key("target", BtValueType::Entity), key("alert", BtValueType::Bool, false), key("speed", BtValueType::Float, 2.5), key("home", BtValueType::Vec3, Json::array({ 1, 2, 3 })), key("count", BtValueType::Int, 4), key("label", BtValueType::String, "guard") };
			doc.Nodes = {
				node("root", "Selector", { "chase", "patrol" }, { { "reactive", true } }),
				node("chase", "Sequence", { "guard", "go" }),
				node("guard", "Guard", { "go_inner" }, { { "key", "alert" }, { "op", "true" } }),
				node("go_inner", "Wait", {}, { { "seconds", 0.5 } }),
				node("go", "MoveTo", {}, { { "target", "key:target" }, { "stoppingDistance", 1.5 } }),
				node("patrol", "Sequence", { "set", "check", "wait", "log" }),
				node("set", "SetBlackboard", {}, { { "key", "speed" }, { "value", 3.0 } }),
				node("check", "CheckBlackboard", {}, { { "key", "count" }, { "op", ">=" }, { "value", 2 } }),
				node("wait", "Wait", {}, { { "min", 1.0 }, { "max", 2.0 } }),
				node("log", "Log", {}, { { "message", "patrolled" } }),
			};
			return doc;
		}

		// Whether a problem with a message that holds `text` is on the node (an empty id: the tree).
		bool has_problem(const std::vector<BtProblem>& problems, const std::string& nodeId, const std::string& text, bool error = true)
		{
			return std::any_of(problems.begin(), problems.end(), [&](const BtProblem& p) { return p.NodeId == nodeId && p.Error == error && p.Message.find(text) != std::string::npos; });
		}

		void file_format(Check& check)
		{
			const BehaviorTreeDoc doc = example_tree();
			check(!bt_has_errors(validate_bt(doc)), "bt_valid: the example tree has no errors");

			const std::string text = write_bt_file(doc);
			std::string error;
			const std::optional<BehaviorTreeDoc> back = read_bt_file(text, error);
			check(back && *back == doc, "bt_roundtrip: write then read gives the same tree (" + error + ")");
			check(back && write_bt_file(*back) == text, "bt_roundtrip: the text is stable");

			for (const char* garbage : { "", "not json", "[]", "{}", "{\"version\": 2, \"root\": \"a\", \"nodes\": []}", "{\"version\": 1, \"nodes\": []}",
				"{\"version\": 1, \"root\": \"a\"}", "{\"version\": 1, \"root\": \"a\", \"nodes\": [{\"id\": 5, \"type\": \"Wait\"}]}",
				"{\"version\": 1, \"root\": \"a\", \"nodes\": [], \"blackboard\": [{\"key\": \"k\", \"type\": \"matrix\"}]}",
				"{\"version\": 1, \"root\": \"a\", \"nodes\": [{\"id\": \"a\", \"type\": \"Wait\", \"children\": [3]}]}" })
			{
				error.clear();
				check(!read_bt_file(garbage, error).has_value() && !error.empty(), std::string("bt_rejects_garbage: refused with a reason: ") + garbage);
			}
		}

		void validation(Check& check)
		{
			{
				BehaviorTreeDoc doc = example_tree();
				doc.Root.clear();
				check(has_problem(validate_bt(doc), "", "no root"), "validate_root: a tree without a root");
				doc.Root = "ghost";
				check(has_problem(validate_bt(doc), "", "not a node"), "validate_root: a root that is not a node");
			}
			{
				BehaviorTreeDoc doc;
				doc.Root = "a";
				doc.Nodes = { node("a", "Sequence", { "b" }), node("b", "Inverter", { "a" }) };
				check(has_problem(validate_bt(doc), "a", "more than one node, or part of a cycle"), "validate_cycle: a cycle is named at the node that closes it");
			}
			{
				BehaviorTreeDoc doc;
				doc.Root = "a";
				doc.Nodes = { node("a", "Sequence", { "b", "c" }), node("b", "Inverter", { "d" }), node("c", "Inverter", { "d" }), node("d", "Wait") };
				check(has_problem(validate_bt(doc), "d", "more than one node"), "validate_shared: a node with two parents");
			}
			{
				BehaviorTreeDoc doc;
				doc.Root = "a";
				doc.Nodes = { node("a", "Parallel", { "b" }), node("b", "Wait") };
				check(has_problem(validate_bt(doc), "a", "at least 2 children"), "validate_children: a Parallel with one child");
				doc.Nodes = { node("a", "Inverter", { "b", "c" }), node("b", "Wait"), node("c", "Wait") };
				check(has_problem(validate_bt(doc), "a", "exactly 1 child"), "validate_children: a decorator with two children");
				doc.Nodes = { node("a", "Sequence"), node("b", "Wait") };
				check(has_problem(validate_bt(doc), "a", "at least 1 children"), "validate_children: a Sequence without children");
				doc.Nodes = { node("a", "Wait", { "b" }), node("b", "Wait") };
				check(has_problem(validate_bt(doc), "a", "no children"), "validate_children: a leaf with a child");
				doc.Nodes = { node("a", "Sequence", { "nope" }) };
				check(has_problem(validate_bt(doc), "a", "is not a node"), "validate_children: a child that does not exist");
			}
			{
				BehaviorTreeDoc doc;
				doc.Root = "a";
				doc.Nodes = { node("a", "Dance") };
				check(has_problem(validate_bt(doc), "a", "unknown node type"), "validate_type: an unknown node type");
				doc.Nodes = { node("a", "Sequence", { "b" }), node("b", "Wait"), node("b", "Wait") };
				check(has_problem(validate_bt(doc), "b", "id is used"), "validate_ids: a duplicate id");
				doc.Nodes = { node("a", "Wait"), node("loose", "Wait") };
				check(has_problem(validate_bt(doc), "loose", "does not reach", false) && !bt_has_errors(validate_bt(doc)), "validate_unreached: a node the root does not reach is a warning");
			}
			{
				BehaviorTreeDoc doc = example_tree();
				doc.Nodes[2].Params["key"] = "nope";
				check(has_problem(validate_bt(doc), "guard", "declared blackboard key"), "validate_key: a key that is not declared");
				doc = example_tree();
				doc.Nodes[7].Params["key"] = "alert"; // a bool compared with >=
				check(has_problem(validate_bt(doc), "check", "does not fit the bool key"), "validate_key: a comparison that does not fit the type");
				doc = example_tree();
				doc.Nodes[6].Params["value"] = "fast"; // a string for a float key
				check(has_problem(validate_bt(doc), "set", "does not fit the float key"), "validate_key: a value of the wrong type");
				doc = example_tree();
				doc.Nodes[4].Params["target"] = "key:speed"; // a float key as a MoveTo target
				check(has_problem(validate_bt(doc), "go", "vec3 or entity key"), "validate_key: a target key of the wrong type");
				doc = example_tree();
				doc.Nodes[4].Params["target"] = Json::array({ 1, 2 });
				check(has_problem(validate_bt(doc), "go", "[x, y, z]"), "validate_param: a target that is neither a vector nor a key");
				doc = example_tree();
				doc.Nodes[3].Params["seconds"] = -1;
				check(has_problem(validate_bt(doc), "go_inner", "at least 0"), "validate_param: a negative number");
				doc = example_tree();
				doc.Nodes[6].Params.erase("key");
				check(has_problem(validate_bt(doc), "set", "missing parameter"), "validate_param: a missing required parameter");
				doc = example_tree();
				doc.Nodes[9].Params["colour"] = "red";
				check(has_problem(validate_bt(doc), "log", "unknown parameter", false), "validate_param: an unknown parameter is a warning");
				doc = example_tree();
				doc.Nodes.push_back(node("lua", "LuaAction", {}, { { "script", "nothex" } }));
				doc.Nodes[5].Children = {};
				doc.Nodes[1].Children = { "guard", "go", "lua" };
				check(has_problem(validate_bt(doc), "lua", "guid of a script"), "validate_param: a Lua leaf without a script guid");
			}
			{
				BehaviorTreeDoc doc = example_tree();
				doc.Blackboard.push_back(key("speed", BtValueType::Int));
				check(has_problem(validate_bt(doc), "", "declared twice"), "validate_blackboard: a key declared twice");
				doc = example_tree();
				doc.Blackboard.push_back(key("", BtValueType::Int));
				check(has_problem(validate_bt(doc), "", "no name"), "validate_blackboard: a key without a name");
				doc = example_tree();
				doc.Blackboard.push_back(key("bad", BtValueType::Vec3, Json::array({ 1, 2 })));
				check(has_problem(validate_bt(doc), "", "does not fit its type"), "validate_blackboard: a default that does not fit");
			}
		}

		void asset_wiring(Check& check)
		{
			BehaviorTreeData data;
			data.Text = write_bt_file(example_tree());
			const std::vector<std::byte> bytes = serialize(data);
			const std::optional<BehaviorTreeData> back = deserialize<BehaviorTreeData>(bytes);
			check(back && back->Text == data.Text, "bt_archive: the asset round trips");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<BehaviorTreeData>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "bt_archive: a truncated asset is refused");
			std::vector<std::byte> trailing = bytes;
			trailing.push_back(std::byte{ 0 });
			check(!deserialize<BehaviorTreeData>(trailing).has_value(), "bt_archive: a trailing byte is refused");
			BehaviorTreeData empty;
			check(!deserialize<BehaviorTreeData>(serialize(empty)).has_value(), "bt_archive: an empty text is refused");
			BehaviorTreeData garbage;
			garbage.Text = "not a tree";
			check(!deserialize<BehaviorTreeData>(serialize(garbage)).has_value(), "bt_archive: text that is not a tree is refused");

			check(std::string(asset_type_name(AssetType::BehaviorTree)) == "BehaviorTree" && static_cast<u32>(AssetType::BehaviorTree) + 1 == static_cast<u32>(AssetType::Last), "bt_asset_type: BehaviorTree is the last type before Last");
		}
	}

	bool run_ai_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		file_format(check);
		validation(check);
		asset_wiring(check);

		if (check.Failures == 0)
		{
			WI_AI_LOG(Info, "AI smoke test passed");
		}
		return check.Failures == 0;
	}
}
