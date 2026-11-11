#include <editor_common.h>
#include "ai_smoke_test.h"

#include <algorithm>

#include <map>

#include <ai/blackboard.h>
#include <ai/bt_compile.h>
#include <ai/bt_file.h>
#include <ai/bt_instance.h>
#include <ai/ai_module.h>
#include <ai/bt_leaves_engine.h>
#include <scene/scene_clone.h>
#include <scene/scene_components.h>
#include <nav/nav_module.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <assets/wasset_io.h>

#include "../asset_pipeline/nav_bake.h"
#include "../nav/nav_fixtures.h"

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

	namespace
	{
		// External leaves that answer from a script, by node id, and count what they were asked.
		struct FakeLeaves
		{
			std::map<std::string, std::vector<BtStatus>> Script{};
			std::map<std::string, int> Starts{}, Ticks{}, Aborts{};
			std::map<std::string, size_t> Position{};
			BtServices Services{};

			FakeLeaves()
			{
				Services.Leaf = [this](BtLeafCall& call) -> BtStatus
				{
					const std::string& id = call.Node->Id;
					if (call.Phase == BtLeafPhase::Abort)
					{
						Aborts[id]++;
						return BtStatus::Failure;
					}
					size_t& at = Position[id];
					if (call.Phase == BtLeafPhase::Start)
					{
						Starts[id]++;
						at = 0;
					}
					else
					{
						Ticks[id]++;
						at++;
					}
					const std::vector<BtStatus>& script = Script[id];
					if (script.empty())
					{
						return BtStatus::Running;
					}
					return script[std::min(at, script.size() - 1)];
				};
			}
		};

		BtNodeDesc leaf(const std::string& id) { return node(id, "MoveTo", {}, { { "target", Json::array({ 0, 0, 0 }) } }); }

		Ref<const CompiledTree> compiled(Check& check, BehaviorTreeDoc doc)
		{
			std::string error;
			Ref<const CompiledTree> tree = compile_bt(doc, error);
			check(tree != nullptr, "compile: the test tree compiles (" + error + ")");
			return tree;
		}

		constexpr auto SUCCESS = BtStatus::Success;
		constexpr auto FAILURE = BtStatus::Failure;
		constexpr auto RUNNING = BtStatus::Running;

		void runtime_nodes(Check& check)
		{
			const entt::entity self{ 3 };

			// Sequence: memory, and stopping at the first failure.
			{
				FakeLeaves fake;
				fake.Script["a"] = { SUCCESS };
				fake.Script["b"] = { RUNNING, RUNNING, SUCCESS };
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Nodes = { node("s", "Sequence", { "a", "b" }), leaf("a"), leaf("b") };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == RUNNING && instance.tick(fake.Services, 0.1f, 0.1) == RUNNING && instance.tick(fake.Services, 0.1f, 0.2) == SUCCESS, "sequence_memory: Running, Running, then Success");
				check(fake.Starts["a"] == 1 && fake.Starts["b"] == 1 && fake.Ticks["b"] == 2, "sequence_memory: the first child is not run again while the second runs");

				FakeLeaves failing;
				failing.Script["a"] = { SUCCESS };
				failing.Script["b"] = { FAILURE };
				BehaviorTreeDoc stops;
				stops.Root = "s";
				stops.Nodes = { node("s", "Sequence", { "a", "b", "c" }), leaf("a"), leaf("b"), leaf("c") };
				BtInstance second(compiled(check, stops), self);
				check(second.tick(failing.Services, 0.1f, 0.0) == FAILURE && failing.Starts["c"] == 0, "sequence_fail: the first failure ends it, the rest does not run");
			}

			// Selector: falls through failures, keeps the running child.
			{
				FakeLeaves fake;
				fake.Script["a"] = { FAILURE };
				fake.Script["b"] = { RUNNING, SUCCESS };
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Nodes = { node("s", "Selector", { "a", "b", "c" }), leaf("a"), leaf("b"), leaf("c") };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == RUNNING && instance.tick(fake.Services, 0.1f, 0.1) == SUCCESS, "selector_memory: falls through a failure, keeps the running child, then succeeds");
				check(fake.Starts["a"] == 1 && fake.Starts["c"] == 0, "selector_memory: a child that failed is not asked again while a later one runs");
			}

			// Reactive and plain selector with a guard that changes.
			for (const bool reactive : { true, false })
			{
				FakeLeaves fake;
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Blackboard = { key("alert", BtValueType::Bool, false) };
				doc.Nodes = { node("s", "Selector", { "g", "y" }, { { "reactive", reactive } }), node("g", "Guard", { "x" }, { { "key", "alert" }, { "op", "true" } }), leaf("x"), leaf("y") };
				BtInstance instance(compiled(check, doc), self);
				const std::string mode = reactive ? "reactive" : "plain";
				check(instance.tick(fake.Services, 0.1f, 0.0) == RUNNING && fake.Starts["y"] == 1 && fake.Starts["x"] == 0, mode + "_selector: the lower priority child runs while the guard is false");
				instance.blackboard().set("alert", true);
				instance.tick(fake.Services, 0.1f, 0.1);
				if (reactive)
				{
					check(fake.Aborts["y"] == 1 && fake.Starts["x"] == 1, "reactive_selector: a higher priority child that now runs aborts the running one, once");
					instance.blackboard().set("alert", false);
					instance.tick(fake.Services, 0.1f, 0.2);
					check(fake.Aborts["x"] == 1 && fake.Starts["y"] == 2, "reactive_selector: the guard going false cuts its child and the other one starts again");
				}
				else
				{
					check(fake.Aborts["y"] == 0 && fake.Starts["x"] == 0 && fake.Ticks["y"] == 1, "plain_selector: the running child is not interrupted");
				}
			}

			// Reactive sequence: a condition that stops holding aborts what runs after it.
			{
				FakeLeaves fake;
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Blackboard = { key("alert", BtValueType::Bool, true) };
				doc.Nodes = { node("s", "Sequence", { "c", "m" }, { { "reactive", true } }), node("c", "CheckBlackboard", {}, { { "key", "alert" }, { "op", "true" } }), leaf("m") };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == RUNNING && fake.Starts["m"] == 1, "reactive_sequence: runs while the condition holds");
				instance.blackboard().set("alert", false);
				check(instance.tick(fake.Services, 0.1f, 0.1) == FAILURE && fake.Aborts["m"] == 1 && !instance.running(), "reactive_sequence: the condition failing aborts the running child once and fails");
			}

			// Parallel policies.
			{
				FakeLeaves all;
				all.Script["a"] = { RUNNING, SUCCESS };
				all.Script["b"] = { RUNNING, RUNNING, SUCCESS };
				BehaviorTreeDoc doc;
				doc.Root = "p";
				doc.Nodes = { node("p", "Parallel", { "a", "b" }), leaf("a"), leaf("b") };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(all.Services, 0.1f, 0.0) == RUNNING && instance.tick(all.Services, 0.1f, 0.1) == RUNNING && instance.tick(all.Services, 0.1f, 0.2) == SUCCESS, "parallel_all: succeeds when every child has");
				check(all.Starts["a"] == 1 && all.Ticks["a"] == 1, "parallel_all: a child that finished is not run again");

				FakeLeaves one;
				one.Script["a"] = { RUNNING, SUCCESS };
				BehaviorTreeDoc first;
				first.Root = "p";
				first.Nodes = { node("p", "Parallel", { "a", "b" }, { { "success", "one" } }), leaf("a"), leaf("b") };
				BtInstance quick(compiled(check, first), self);
				quick.tick(one.Services, 0.1f, 0.0);
				check(quick.tick(one.Services, 0.1f, 0.1) == SUCCESS && one.Aborts["b"] == 1, "parallel_one: succeeds when one child does and aborts the others");

				FakeLeaves fails;
				fails.Script["a"] = { RUNNING, FAILURE };
				BehaviorTreeDoc bad;
				bad.Root = "p";
				bad.Nodes = { node("p", "Parallel", { "a", "b" }), leaf("a"), leaf("b") };
				BtInstance failing(compiled(check, bad), self);
				failing.tick(fails.Services, 0.1f, 0.0);
				check(failing.tick(fails.Services, 0.1f, 0.1) == FAILURE && fails.Aborts["b"] == 1, "parallel_failure: one failure fails it and aborts the others");
			}

			// Decorators.
			{
				FakeLeaves fake;
				fake.Script["ok"] = { SUCCESS };
				fake.Script["no"] = { FAILURE };
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Nodes = { node("s", "Sequence", { "i", "u" }), node("i", "Inverter", { "no" }), node("u", "Succeeder", { "no2" }), leaf("no"), leaf("no2") };
				fake.Script["no2"] = { FAILURE };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == SUCCESS, "inverter_succeeder: an inverted failure and a forced success");

				BehaviorTreeDoc inverted;
				inverted.Root = "i";
				inverted.Nodes = { node("i", "Inverter", { "ok" }), leaf("ok") };
				BtInstance second(compiled(check, inverted), self);
				check(second.tick(fake.Services, 0.1f, 0.0) == FAILURE, "inverter: an inverted success fails");
			}

			// Repeat: three times, for ever, and a failure ends it.
			{
				FakeLeaves fake;
				fake.Script["a"] = { SUCCESS };
				BehaviorTreeDoc doc;
				doc.Root = "r";
				doc.Nodes = { node("r", "Repeat", { "a" }, { { "count", 3 } }), leaf("a") };
				BtInstance three(compiled(check, doc), self);
				const BtStatus a = three.tick(fake.Services, 0.1f, 0.0);
				const BtStatus b = three.tick(fake.Services, 0.1f, 0.1);
				const BtStatus c = three.tick(fake.Services, 0.1f, 0.2);
				check(a == RUNNING && b == RUNNING && c == SUCCESS && fake.Starts["a"] == 3, "repeat_count: three runs, then Success");
				doc.Nodes[0].Params["count"] = 0;
				BtInstance forever(compiled(check, doc), self);
				bool always = true;
				for (int i = 0; i < 20; i++)
				{
					always = always && forever.tick(fake.Services, 0.1f, 0.1 * i) == RUNNING;
				}
				check(always, "repeat_forever: Running for ever");
				fake.Script["a"] = { FAILURE };
				BtInstance failing(compiled(check, doc), self);
				check(failing.tick(fake.Services, 0.1f, 0.0) == FAILURE, "repeat_failure: a failing child ends it");
			}

			// Cooldown.
			{
				FakeLeaves fake;
				fake.Script["a"] = { SUCCESS };
				BehaviorTreeDoc doc;
				doc.Root = "c";
				doc.Nodes = { node("c", "Cooldown", { "a" }, { { "seconds", 5.0 } }), leaf("a") };
				BtInstance instance(compiled(check, doc), self);
				const BtStatus first = instance.tick(fake.Services, 0.1f, 0.0);
				const BtStatus within = instance.tick(fake.Services, 0.1f, 1.0);
				const BtStatus after = instance.tick(fake.Services, 0.1f, 6.0);
				check(first == SUCCESS && within == FAILURE && after == SUCCESS && fake.Starts["a"] == 2, "cooldown: runs, is refused inside the window, runs again after it");
			}

			// Wait, and the seeded random range.
			{
				FakeLeaves fake;
				BehaviorTreeDoc doc;
				doc.Root = "w";
				doc.Nodes = { node("w", "Wait", {}, { { "seconds", 2.0 } }) };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == RUNNING && instance.tick(fake.Services, 0.1f, 1.0) == RUNNING && instance.tick(fake.Services, 0.1f, 2.0) == SUCCESS, "wait: Running until the time is up");

				doc.Nodes = { node("w", "Wait", {}, { { "min", 1.0 }, { "max", 3.0 } }) };
				const Ref<const CompiledTree> tree = compiled(check, doc);
				const auto finish_time = [&](u32 seed)
				{
					BtInstance random(tree, self, seed);
					double time = 0.0;
					while (random.tick(fake.Services, 0.1f, time) == RUNNING && time < 10.0)
					{
						time += 0.01;
					}
					return time;
				};
				const double one = finish_time(7);
				check(one >= 1.0 && one <= 3.01 && finish_time(7) == one && finish_time(8) != one, "wait_random: a time in the range, the same for a seed, another for another seed");
				BtInstance zero(compiled(check, BehaviorTreeDoc{ "w", { node("w", "Wait") }, {} }), self);
				check(zero.tick(fake.Services, 0.1f, 0.0) == SUCCESS, "wait_zero: no time given is Success at once");
			}

			// SetBlackboard, CheckBlackboard and the comparisons of every type.
			{
				FakeLeaves fake;
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Blackboard = { key("count", BtValueType::Int, 1), key("name", BtValueType::String, "a"), key("home", BtValueType::Vec3, Json::array({ 1, 2, 3 })), key("on", BtValueType::Bool, false) };
				doc.Nodes = { node("s", "Sequence", { "set", "c1", "c2", "c3", "c4", "c5" }),
					node("set", "SetBlackboard", {}, { { "key", "count" }, { "value", 7 } }),
					node("c1", "CheckBlackboard", {}, { { "key", "count" }, { "op", ">" }, { "value", 6 } }),
					node("c2", "CheckBlackboard", {}, { { "key", "count" }, { "op", "!=" }, { "value", 8 } }),
					node("c3", "CheckBlackboard", {}, { { "key", "name" }, { "op", "==" }, { "value", "a" } }),
					node("c4", "CheckBlackboard", {}, { { "key", "home" }, { "op", "==" }, { "value", Json::array({ 1, 2, 3 }) } }),
					node("c5", "CheckBlackboard", {}, { { "key", "on" }, { "op", "false" } }) };
				BtInstance instance(compiled(check, doc), self);
				check(instance.tick(fake.Services, 0.1f, 0.0) == SUCCESS && std::get<i32>(*instance.blackboard().get("count")) == 7, "blackboard_nodes: set and the comparisons of int, string, vec3 and bool hold");
				doc.Nodes[2].Params["op"] = "<";
				BtInstance failing(compiled(check, doc), self);
				check(failing.tick(fake.Services, 0.1f, 0.0) == FAILURE, "blackboard_nodes: a comparison that is false fails the sequence");
			}

			// The blackboard itself.
			{
				Blackboard board;
				board.declare({ key("f", BtValueType::Float, 1.5), key("e", BtValueType::Entity), key("v", BtValueType::Vec3) });
				std::string error;
				check(std::get<float>(*board.get("f")) == 1.5f && !std::get<EntityGuid>(*board.get("e")).valid() && std::get<glm::vec3>(*board.get("v")) == glm::vec3(0.0f), "blackboard_defaults: the declared defaults, zero where none");
				check(!board.set("f", true, &error) && error.find("float") != std::string::npos, "blackboard_type: a value of the wrong type is refused naming the type");
				check(!board.set("nope", 1.0f, &error) && error.find("no key") != std::string::npos, "blackboard_unknown: an unknown key is refused");
				check(!board.set("f", std::numeric_limits<float>::quiet_NaN(), &error), "blackboard_nan: a float that is not a number is refused");
				check(board.set("f", 2.0f) && std::get<float>(*board.get("f")) == 2.0f, "blackboard_set: a value of the right type is kept");
				check(board.set_dynamic("seen", EntityGuid::derive(EntityGuid{ 1, 2 }, 3)) && board.type_of("seen") == BtValueType::Entity && !board.declared("seen") && !board.set_dynamic("seen", 1.0f), "blackboard_dynamic: an undeclared key is made by set_dynamic, typed by its first value");
				const std::vector<std::string> problems = board.apply_overrides({ { "f", 9.0 }, { "ghost", 1 }, { "v", "x" } });
				check(std::get<float>(*board.get("f")) == 9.0f && problems.size() == 2, "blackboard_overrides: a valid override applies, an unknown key and a wrong value are reported");
			}

			// Compile refuses an invalid tree; instances of one tree are independent; abort is once; no services fails.
			{
				BehaviorTreeDoc cycle;
				cycle.Root = "a";
				cycle.Nodes = { node("a", "Sequence", { "b" }), node("b", "Inverter", { "a" }) };
				std::string error;
				check(compile_bt(cycle, error) == nullptr && error.find("cycle") != std::string::npos, "compile_invalid: an invalid tree is refused with the validator's message");

				FakeLeaves fake;
				BehaviorTreeDoc doc;
				doc.Root = "s";
				doc.Blackboard = { key("n", BtValueType::Int, 0) };
				doc.Nodes = { node("s", "Sequence", { "a" }), leaf("a") };
				const Ref<const CompiledTree> tree = compiled(check, doc);
				BtInstance one(tree, entt::entity{ 1 });
				BtInstance two(tree, entt::entity{ 2 });
				one.tick(fake.Services, 0.1f, 0.0);
				one.blackboard().set("n", i32{ 5 });
				check(one.running() && !two.running() && std::get<i32>(*two.blackboard().get("n")) == 0, "instances_independent: two instances of one tree keep their own state and blackboard");

				one.abort(fake.Services, 0.1);
				one.abort(fake.Services, 0.2);
				check(fake.Aborts["a"] == 1 && !one.running(), "abort_once: aborting twice aborts the running leaf once");
				one.tick(fake.Services, 0.1f, 0.3);
				one.restart(fake.Services, 0.4);
				check(fake.Aborts["a"] == 2 && std::get<i32>(*one.blackboard().get("n")) == 0, "restart: aborts the running leaf and puts the blackboard back");

				BtServices none;
				BtInstance orphan(tree, entt::entity{ 4 });
				check(orphan.tick(none, 0.1f, 0.0) == FAILURE, "external_without_services: a leaf with nothing to run it fails");
			}
		}
	}

	namespace
	{
		BtNodeDesc move_node(const std::string& id, Json target, double stopping = 0.0)
		{
			Json params = { { "target", std::move(target) } };
			if (stopping > 0.0)
			{
				params["stoppingDistance"] = stopping;
			}
			return node(id, "MoveTo", {}, params);
		}

		// A scene with an agent at (-8, 0, 0), a goal entity at (8, 0, 0), and a navigation module with a baked floor (with a wall of halfZ, 0: none).
		struct MoveFixture
		{
			NavMeshData Mesh{};
			Ref<Scene> TheScene = CreateRef<Scene>();
			NavModule Nav;
			entt::entity Agent = entt::null;
			entt::entity Goal = entt::null;
			BtEngineHooks Hooks{};
			BtServices Services{};
			double Time = 0.0;
			bool Ok = true;

			explicit MoveFixture(float halfZ, bool withMesh = true)
			{
				if (withMesh)
				{
					const NavBakeResult baked = bake_navmesh(nav_fixtures::floor_with_wall(halfZ));
					Ok = baked.Ok;
					Mesh = baked.Mesh;
				}
				Agent = TheScene->create("Agent");
				TheScene->registry().get<Transform>(Agent).Position = glm::vec3(-8.0f, 0.0f, 0.0f);
				TheScene->registry().emplace<NavAgent>(Agent);
				Goal = TheScene->create("Goal");
				TheScene->registry().get<Transform>(Goal).Position = glm::vec3(8.0f, 0.0f, 0.0f);
				TheScene->update_world_transforms();
				Nav.set_auto_start(false);
				if (withMesh)
				{
					Nav.start_world_with_mesh(TheScene, Mesh);
				}
				else
				{
					Nav.start_world(TheScene);
				}
				Hooks.TheScene = TheScene.get();
				Hooks.Nav = &Nav;
				Services.Leaf = [this](BtLeafCall& call) { return bt_engine_leaf(call, Hooks); };
			}

			// One frame: the tree, then the navigation (the engine's order); returns what the tree said.
			BtStatus frame(BtInstance& instance)
			{
				const BtStatus status = instance.tick(Services, 1.0f / 60.0f, Time);
				Nav.tick(nullptr, true, 1.0f / 60.0f);
				Time += 1.0 / 60.0;
				return status;
			}

			glm::vec3 position() const { return TheScene->registry().get<Transform>(Agent).Position; }
		};

		void engine_leaves(Check& check)
		{
			// Arrives.
			{
				MoveFixture f(0.0f);
				check(f.Ok, "moveto_fixture: the floor bakes");
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Nodes = { move_node("m", Json::array({ 8, 0, 0 })) };
				BtInstance instance(compiled(check, doc), f.Agent);
				BtStatus status = f.frame(instance);
				check(status == RUNNING, "moveto_arrive: Running when the walk starts");
				for (int i = 0; i < 1200 && status == RUNNING; i++)
				{
					status = f.frame(instance);
				}
				check(status == SUCCESS && glm::distance(f.position(), glm::vec3(8.0f, 0.0f, 0.0f)) < 0.6f, "moveto_arrive: Success at the target");
			}

			// A wall across the whole floor: the walk goes as far as it can, then fails.
			{
				MoveFixture f(10.0f);
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Nodes = { move_node("m", Json::array({ 8, 0, 0 })) };
				BtInstance instance(compiled(check, doc), f.Agent);
				BtStatus status = RUNNING;
				for (int i = 0; i < 1200 && status == RUNNING; i++)
				{
					status = f.frame(instance);
				}
				check(status == FAILURE && f.position().x < -0.4f, "moveto_unreachable: Failure, and the agent stayed on its side of the wall");
			}

			// An entity target that is destroyed mid-walk fails; one that moves is followed.
			{
				MoveFixture f(0.0f);
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Blackboard = { key("target", BtValueType::Entity) };
				doc.Nodes = { move_node("m", "key:target") };
				BtInstance instance(compiled(check, doc), f.Agent);
				instance.blackboard().set("target", f.TheScene->registry().get<Id>(f.Goal).Guid);
				BtStatus status = RUNNING;
				for (int i = 0; i < 20; i++)
				{
					status = f.frame(instance);
				}
				f.TheScene->registry().get<Transform>(f.Goal).Position = glm::vec3(8.0f, 0.0f, 6.0f);
				f.TheScene->update_world_transforms();
				for (int i = 0; i < 1200 && status == RUNNING; i++)
				{
					status = f.frame(instance);
				}
				check(status == SUCCESS && glm::distance(f.position(), glm::vec3(8.0f, 0.0f, 6.0f)) < 0.8f, "moveto_follows: an entity target that moved is followed to its new place");

				MoveFixture g(0.0f);
				BtInstance second(compiled(check, doc), g.Agent);
				second.blackboard().set("target", g.TheScene->registry().get<Id>(g.Goal).Guid);
				BtStatus ongoing = RUNNING;
				for (int i = 0; i < 10; i++)
				{
					ongoing = g.frame(second);
				}
				g.TheScene->destroy(g.Goal);
				ongoing = g.frame(second);
				check(ongoing == FAILURE && g.Nav.state(g.Agent) == NavAgentState::Idle, "moveto_target_destroyed: a target that is gone fails the node and stops the agent");

				MoveFixture h(0.0f);
				BtInstance empty(compiled(check, doc), h.Agent);
				check(h.frame(empty) == FAILURE, "moveto_empty_key: an empty entity key fails");
			}

			// Off the mesh, a point that is not a number, a key that is not a point.
			{
				MoveFixture f(0.0f);
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Blackboard = { key("spot", BtValueType::Vec3), key("size", BtValueType::Float) };
				doc.Nodes = { move_node("m", "key:spot") };
				BtInstance instance(compiled(check, doc), f.Agent);
				instance.blackboard().set("spot", glm::vec3(500.0f, 0.0f, 500.0f));
				check(f.frame(instance) == FAILURE, "moveto_off_mesh: a point far from the navmesh fails");
				std::string error;
				check(!instance.blackboard().set("spot", glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f), &error), "moveto_nan: a point that is not a number cannot be put on the blackboard");
				BehaviorTreeDoc wrong = doc;
				wrong.Nodes = { move_node("m", "key:size") };
				std::string compileError;
				check(compile_bt(wrong, compileError) == nullptr, "moveto_wrong_key: a float key as a target does not compile");
			}

			// Abort stops the agent; stopping distance ends the walk early.
			{
				MoveFixture f(0.0f);
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Nodes = { move_node("m", Json::array({ 8, 0, 0 })) };
				BtInstance instance(compiled(check, doc), f.Agent);
				for (int i = 0; i < 30; i++)
				{
					f.frame(instance);
				}
				check(f.Nav.state(f.Agent) == NavAgentState::Moving, "moveto_abort: the agent walks");
				instance.abort(f.Services, f.Time);
				check(f.Nav.state(f.Agent) == NavAgentState::Idle && !instance.running(), "moveto_abort: an abort stops the agent");

				MoveFixture g(0.0f);
				doc.Nodes = { move_node("m", Json::array({ 8, 0, 0 }), 3.0) };
				BtInstance closer(compiled(check, doc), g.Agent);
				BtStatus status = RUNNING;
				for (int i = 0; i < 1200 && status == RUNNING; i++)
				{
					status = g.frame(closer);
				}
				const float left = glm::distance(g.position(), glm::vec3(8.0f, 0.0f, 0.0f));
				check(status == SUCCESS && left > 2.0f && left < 3.6f, "moveto_stopping_distance: Success within the stopping distance (" + std::to_string(left) + " m left)");
			}

			// No agent, no navmesh.
			{
				MoveFixture f(0.0f);
				f.TheScene->registry().remove<NavAgent>(f.Agent);
				BehaviorTreeDoc doc;
				doc.Root = "m";
				doc.Nodes = { move_node("m", Json::array({ 8, 0, 0 })) };
				BtInstance instance(compiled(check, doc), f.Agent);
				check(f.frame(instance) == FAILURE && instance.states()[0].Warned, "moveto_no_agent: an entity without a NavAgent fails, once logged");

				MoveFixture g(0.0f, false);
				BtInstance blind(compiled(check, doc), g.Agent);
				check(g.frame(blind) == FAILURE && blind.states()[0].Warned, "moveto_no_navmesh: a scene without a navmesh fails, once logged");
			}

			// PlayAnimation.
			{
				BtEngineHooks hooks;
				int busy = 3;
				hooks.PlayClip = [](entt::entity, const std::string& clip) { return clip != "Nope"; };
				hooks.ClipBusy = [&busy](entt::entity) { return busy-- > 0; };
				BtServices services;
				services.Leaf = [&](BtLeafCall& call) { return bt_engine_leaf(call, hooks); };
				BehaviorTreeDoc doc;
				doc.Root = "p";
				doc.Nodes = { node("p", "PlayAnimation", {}, { { "clip", "Wave" }, { "wait", true } }) };
				BtInstance waits(compiled(check, doc), entt::entity{ 1 });
				const BtStatus a = waits.tick(services, 0.1f, 0.0);
				const BtStatus b = waits.tick(services, 0.1f, 0.1);
				BtStatus last = b;
				for (int i = 0; i < 10 && last == RUNNING; i++)
				{
					last = waits.tick(services, 0.1f, 0.2 + i);
				}
				check(a == RUNNING && b == RUNNING && last == SUCCESS, "playanimation_wait: Running while the clip is busy, then Success");
				doc.Nodes = { node("p", "PlayAnimation", {}, { { "clip", "Wave" } }) };
				BtInstance quick(compiled(check, doc), entt::entity{ 1 });
				check(quick.tick(services, 0.1f, 0.0) == SUCCESS, "playanimation_nowait: Success at once without wait");
				doc.Nodes = { node("p", "PlayAnimation", {}, { { "clip", "Nope" } }) };
				BtInstance unknown(compiled(check, doc), entt::entity{ 1 });
				check(unknown.tick(services, 0.1f, 0.0) == FAILURE, "playanimation_unknown: an unknown clip fails");
			}
		}
	}

	namespace
	{
		const char* const FAKE_SCRIPT = "00000000000000000000000000000001";

		// External leaves the module hands to a handler (LuaAction nodes): counts per node id, always Running.
		struct CountingLeaves
		{
			std::map<std::string, int> Starts{}, Ticks{}, Aborts{};

			void install(AIModule& module)
			{
				module.set_leaf_handler([this](BtLeafCall& call) -> BtStatus
				{
					const std::string& id = call.Node->Id;
					switch (call.Phase)
					{
					case BtLeafPhase::Start: Starts[id]++; break;
					case BtLeafPhase::Tick: Ticks[id]++; break;
					case BtLeafPhase::Abort: Aborts[id]++; break;
					}
					return BtStatus::Running;
				});
			}
		};

		BehaviorTreeDoc running_tree(const std::string& leafId = "a")
		{
			BehaviorTreeDoc doc;
			doc.Root = "s";
			doc.Blackboard = { key("n", BtValueType::Int, 1) };
			doc.Nodes = { node("s", "Sequence", { leafId }), node(leafId, "LuaAction", {}, { { "script", FAKE_SCRIPT } }) };
			return doc;
		}

		entt::entity add_controller(Scene& scene, const AssetRef& tree, const std::string& name = "AI")
		{
			const entt::entity entity = scene.create(name);
			scene.registry().emplace<AIController>(entity).Tree = tree;
			return entity;
		}

		void controllers(Check& check)
		{
			register_scene_components();
			const AssetRef tree{ AssetGuid::generate(), 0 };

			// A controller is made and ticked; its running leaf is aborted once when it goes.
			{
				AIModule module;
				module.set_auto_start(false);
				CountingLeaves leaves;
				leaves.install(module);
				std::string error;
				check(module.register_tree(tree, running_tree(), &error), "ai_register: a valid tree registers (" + error + ")");
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity ai = add_controller(*scene, tree);
				const entt::entity second = add_controller(*scene, tree, "Second");
				scene->registry().get<AIController>(second).Overrides = "{ \"n\": 5 }";
				check(module.start_world(scene) && module.world_scene() == scene, "ai_world: a world starts for a scene");
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(module.active_count() == 2 && module.instance_of(ai) != nullptr && leaves.Starts["a"] == 2, "ai_controller: every enabled controller gets an instance and starts");
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Ticks["a"] == 2, "ai_controller: a running leaf is ticked every frame");
				check(std::get<i32>(*module.instance_of(ai)->blackboard().get("n")) == 1 && std::get<i32>(*module.instance_of(second)->blackboard().get("n")) == 5, "ai_overrides: two controllers share a tree with their own blackboards, overrides apply");

				// A controller whose tree changes restarts.
				BehaviorTreeDoc changed = running_tree("b");
				check(module.register_tree(tree, changed), "ai_recompile: a changed tree registers");
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["a"] == 2 && leaves.Starts["b"] == 2 && module.active_count() == 2, "ai_recompile: controllers restart on the new tree, the old leaf is aborted once");
				check(!module.register_tree(tree, BehaviorTreeDoc{}, &error) && !error.empty(), "ai_register: an invalid tree is refused with a reason");
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["b"] == 0, "ai_register: a refused tree leaves the running one alone");

				// Disabled: aborted once, no instance; enabled again: a new run.
				scene->registry().get<AIController>(ai).Enabled = false;
				module.tick(nullptr, true, 1.0f / 60.0f);
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["b"] == 1 && module.instance_of(ai) == nullptr, "ai_disabled: a disabled controller aborts its leaf once and has no instance");
				scene->registry().get<AIController>(ai).Enabled = true;
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(module.instance_of(ai) != nullptr && leaves.Starts["b"] == 3, "ai_disabled: enabled again it runs from the start");

				// Destroyed entity, removed component.
				scene->destroy(ai);
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["b"] == 2 && module.instance_of(ai) == nullptr && module.active_count() == 1, "ai_destroyed: a destroyed entity aborts once and leaves no instance");
				scene->registry().remove<AIController>(second);
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["b"] == 3 && module.active_count() == 0, "ai_removed: a removed component aborts once");
				module.stop_world();
				check(leaves.Aborts["b"] == 3 && module.world_scene() == nullptr, "ai_stop_world: nothing left to abort");
			}

			// A clone that plays and stops (the Editor Play shape), and a scene that is gone.
			{
				AIModule module;
				module.set_auto_start(false);
				CountingLeaves leaves;
				leaves.install(module);
				module.register_tree(tree, running_tree());
				Ref<Scene> scene = CreateRef<Scene>();
				add_controller(*scene, tree);
				Ref<Scene> copy = clone_scene(*scene);
				module.start_world(copy);
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(module.active_count() == 1 && leaves.Starts["a"] == 1, "ai_clone: the clone's controller runs");
				module.stop_world();
				check(leaves.Aborts["a"] == 1 && module.active_count() == 0, "ai_clone: Stop aborts what runs, once");

				module.start_world(copy);
				module.tick(nullptr, true, 1.0f / 60.0f);
				copy.reset();
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(leaves.Aborts["a"] == 2 && module.world_scene() == nullptr && module.active_count() == 0, "ai_gone_scene: a scene that is gone takes its controllers with it (aborted once)");
			}

			// A paused game does not tick; no tree, an unknown tree and bad overrides do not crash.
			{
				AIModule module;
				module.set_auto_start(false);
				CountingLeaves leaves;
				leaves.install(module);
				module.register_tree(tree, running_tree());
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity ai = add_controller(*scene, tree);
				const entt::entity bare = add_controller(*scene, AssetRef{}, "Bare");
				const entt::entity broken = add_controller(*scene, tree, "Broken");
				scene->registry().get<AIController>(broken).Overrides = "not json";
				module.start_world(scene);
				module.tick(nullptr, false, 1.0f / 60.0f);
				check(module.active_count() == 0 && leaves.Starts["a"] == 0, "ai_paused: a paused game starts and ticks nothing");
				module.tick(nullptr, true, 1.0f / 60.0f);
				module.tick(nullptr, true, 1.0f / 60.0f);
				check(module.instance_of(bare) == nullptr && module.instance_of(ai) != nullptr && module.instance_of(broken) != nullptr, "ai_no_tree: a controller without a tree is skipped, bad overrides are ignored");
				module.stop_world();
			}

			// The tick rate: 10 Hz over one second of 60 frames.
			{
				AIModule module;
				module.set_auto_start(false);
				CountingLeaves leaves;
				leaves.install(module);
				module.register_tree(tree, running_tree());
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity ai = add_controller(*scene, tree);
				scene->registry().get<AIController>(ai).TickRate = 10.0f;
				module.start_world(scene);
				for (int i = 0; i < 60; i++)
				{
					module.tick(nullptr, true, 1.0f / 60.0f);
				}
				const int calls = leaves.Starts["a"] + leaves.Ticks["a"];
				check(calls >= 9 && calls <= 11, "ai_tick_rate: 10 Hz gives about ten ticks in a second (" + std::to_string(calls) + ")");
			}
		}
	}

	bool run_ai_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		file_format(check);
		validation(check);
		asset_wiring(check);
		runtime_nodes(check);
		engine_leaves(check);
		controllers(check);

		if (check.Failures == 0)
		{
			WI_AI_LOG(Info, "AI smoke test passed");
		}
		return check.Failures == 0;
	}
}
