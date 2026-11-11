#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bt_types.h"

// The .wbt file: a behaviour tree as JSON ({ "version": 1, "root": id, "blackboard": [{ key, type, default }], "nodes": [{ id, type, name, params, children }] }).
// The text is also the cooked form (BehaviorTreeData); the runtime compiles it (bt_compile.h). validate_bt is the one description of what a tree may be,
// used by the importer, the editor and the checks.
namespace wiwa
{
	constexpr int WBT_VERSION = 1;

	// Parses the text. False, and why in error, for text that is not a JSON object of this shape or version (types of the fields, a key type that does not
	// exist). Whether the tree makes sense is validate_bt's business.
	std::optional<BehaviorTreeDoc> read_bt_file(std::string_view text, std::string& error);
	std::string write_bt_file(const BehaviorTreeDoc& doc);

	// What a node type needs. MaxChildren < 0: any number.
	enum class BtParamKind : u8
	{
		Bool,
		Number,       // int or float
		String,
		Op,           // a comparison: "==", "!=", "<", "<=", ">", ">=", "true", "false"
		Policy,       // "all" or "one"
		Key,          // a declared blackboard key
		Value,        // a literal that must fit the type of the key named by `KeyParam`
		Target,       // [x, y, z] or "key:<name>" of a vec3 or entity key
		Script,       // a 32 hex digit asset guid
	};

	struct BtParamSpec
	{
		std::string Name{};
		BtParamKind Kind = BtParamKind::Bool;
		bool Required = false;
		std::string KeyParam{}; // for Value: the param holding the key
	};

	struct BtNodeSpec
	{
		std::string Type{};
		int MinChildren = 0;
		int MaxChildren = 0;
		std::vector<BtParamSpec> Params{};
		bool Leaf() const { return MaxChildren == 0; }
	};

	// The built-in node types (spec: Sequence, Selector, Parallel, Inverter, Succeeder, Repeat, Cooldown, Guard, Wait, SetBlackboard, CheckBlackboard, Log,
	// MoveTo, PlayAnimation) and the two Lua leaves LuaAction / LuaCondition (param "script": the asset guid; any other param is the script's property).
	const std::vector<BtNodeSpec>& bt_builtin_node_specs();
	const BtNodeSpec* bt_find_node_spec(const std::string& type);

	// Whether a JSON value can be the value of a blackboard key of that type.
	bool bt_json_fits(BtValueType type, const nlohmann::json& value);

	struct BtProblem
	{
		std::string NodeId{};   // empty for a problem of the tree as a whole or of a blackboard key (then Message names the key)
		std::string Message{};
		bool Error = true;      // false: a warning (the tree still runs)
	};

	// Every problem of the tree: a missing or unknown root; duplicate ids; a child that does not exist; a node that is a child twice, or reached through a
	// cycle; the wrong number of children; an unknown node type; a param of the wrong kind or missing; a blackboard key that is not declared or has the wrong
	// type for the node; duplicate or empty blackboard keys; a default that does not fit its key. Nodes the root does not reach are warnings.
	std::vector<BtProblem> validate_bt(const BehaviorTreeDoc& doc);
	bool bt_has_errors(const std::vector<BtProblem>& problems);
}
