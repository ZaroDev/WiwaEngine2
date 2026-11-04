#pragma once

#include "input_codes.h"

#include <string>
#include <vector>

namespace wiwa
{
	enum class ActionType : u8 { Button, Axis1D, Axis2D };

	// How an action reads the raw controls. Part order:
	//   Control:     [0] the control
	//   Composite1D: [0] negative, [1] positive
	//   Composite2D: [0] up, [1] down, [2] left, [3] right
	//   Pair2D:      [0] x, [1] y   (a stick, or the mouse delta, bound to an Axis2D action)
	struct Binding
	{
		enum class Kind : u8 { Control, Composite1D, Composite2D, Pair2D };

		Kind BindingKind = Kind::Control;
		InputControl Parts[4];
		float Scale = 1.0f;
		float Deadzone = 0.0f;
		bool Invert = false;

		bool operator==(const Binding&) const = default;
	};

	struct Action
	{
		std::string Name;
		ActionType Type = ActionType::Button;
		std::vector<Binding> Bindings;
	};

	struct ActionMap
	{
		std::string Name;
		// While this map is on the stack, the controls its bindings use are hidden from the maps below it.
		bool Consume = false;
		std::vector<Action> Actions;
	};

	struct InputMapData
	{
		std::vector<ActionMap> Maps;
	};

	size_t binding_part_count(Binding::Kind kind);
	// "control", "negative", "positive", "up", "down", "left", "right", "x", "y"
	const char* binding_part_name(Binding::Kind kind, size_t part);

	const char* action_type_name(ActionType type);
	bool parse_action_type(std::string_view name, ActionType& out);

	// Button accepts Control; Axis1D accepts Control and Composite1D; Axis2D accepts Composite2D and Pair2D.
	bool action_accepts(ActionType type, Binding::Kind kind);

	// Names must be non-empty and unique per scope (maps in the file, actions in a map), every binding must suit its action, and the
	// scale and deadzone must be usable. On failure `error` names the offender.
	bool input_map_valid(const InputMapData& data, std::string& error);
}
