#pragma once

#include "action_types.h"
#include "input_state.h"

#include <functional>
#include <string_view>

namespace wiwa
{
	struct BindingConflict
	{
		std::string Map;
		std::string Action;
		size_t Binding = 0;
	};

	struct ActionState
	{
		float Value = 0.0f;       // Button: the magnitude of the winning binding; Axis1D: signed; Axis2D: the length of Vector
		glm::vec2 Vector{ 0.0f }; // Axis2D only, length clamped to 1
		bool Down = false;
		bool Pressed = false;
		bool Released = false;
	};

	// Evaluates named actions from the raw input state. Only the maps on the stack are evaluated; a query for a map that is not on the
	// stack (or does not exist) gives a zero state and never throws.
	class ActionSystem
	{
	public:
		// A Button action is down when its winning binding reaches this magnitude.
		static constexpr float PRESS_THRESHOLD = 0.5f;

		// Replaces the maps and clears the state. Stack entries whose maps still exist stay.
		void set_maps(std::vector<ActionMap> maps);
		const std::vector<ActionMap>& maps() const { return m_Maps; }
		const ActionMap* find_map(std::string_view name) const;

		// Pushing a map that is already on the stack moves it to the top. Unknown maps are ignored (and logged).
		void push_map(std::string_view name);
		// Removes the map wherever it is on the stack.
		void pop_map(std::string_view name);
		bool map_active(std::string_view name) const;
		// Top of the stack first.
		std::vector<std::string> active_maps() const;

		// Once per frame, after InputState::update.
		void evaluate(const InputState& input);

		ActionState state(std::string_view map, std::string_view action) const;
		bool has_action(std::string_view map, std::string_view action) const;

		// Rebinding. The maps given to set_maps are the defaults; these edit the current bindings.
		// `part` names a part of the binding (binding_part_name); empty is the first one.
		bool rebind(std::string_view map, std::string_view action, size_t binding, const InputControl& control, std::string_view part = {});
		// Returns the index of the new binding, or SIZE_MAX if the action does not exist or cannot use that kind of binding.
		size_t add_binding(std::string_view map, std::string_view action, const Binding& binding);
		bool remove_binding(std::string_view map, std::string_view action, size_t binding);
		bool reset_action(std::string_view map, std::string_view action);
		void reset_all();
		// Every binding of the map (all parts) that uses the control, except for the action named `exceptAction`. Only reports: the caller
		// decides what to do about it.
		std::vector<BindingConflict> conflicts(std::string_view map, const InputControl& control, std::string_view exceptAction = {}) const;
		// True if the action's bindings differ from the defaults.
		bool modified(std::string_view map, std::string_view action) const;
		const Action* action(std::string_view map, std::string_view name) const;

		// Listening for the next control: the first button-like control with a press edge, or a pad axis past 0.5, is handed to the
		// callback once. Escape cancels without calling it. While listening, all actions read zero.
		void listen_for_control(std::function<void(const InputControl&)> callback);
		void cancel_listen() { m_Listen = nullptr; }
		bool listening() const { return static_cast<bool>(m_Listen); }

	private:
		std::vector<ActionMap> m_Defaults;
		std::vector<ActionMap> m_Maps;
		// Parallel to m_Maps and to each map's Actions.
		std::vector<std::vector<ActionState>> m_States;
		// Bottom first.
		std::vector<std::string> m_Stack;

		int map_index(std::string_view name) const;
		Action* find_action(std::string_view map, std::string_view name);
		const Action* find_action(std::string_view map, std::string_view name) const;
		const Action* find_default(std::string_view map, std::string_view name) const;

		void update_listen(const InputState& input);

		std::function<void(const InputControl&)> m_Listen;
	};
}
