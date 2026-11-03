#pragma once

#include "action_types.h"
#include "input_state.h"

#include <string_view>

namespace wiwa
{
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

	private:
		std::vector<ActionMap> m_Maps;
		// Parallel to m_Maps and to each map's Actions.
		std::vector<std::vector<ActionState>> m_States;
		// Bottom first.
		std::vector<std::string> m_Stack;

		int map_index(std::string_view name) const;
	};
}
