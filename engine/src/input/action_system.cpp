#include <pch.h>
#include "action_system.h"

#include <cmath>
#include <unordered_set>

#define WI_INPUT_LOG(level, ...) WI_LOG(level, "Input", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct BindingResult
		{
			float Value = 0.0f;
			glm::vec2 Vector{ 0.0f };
			bool RawPressed = false;
			bool RawReleased = false;
		};

		uint32_t control_key(const InputControl& control)
		{
			return (static_cast<uint32_t>(control.Device) << 16) | control.Code;
		}

		// (|v| - dz) / (1 - dz) with the sign kept, zero inside the deadzone.
		float apply_deadzone(float value, float deadzone)
		{
			const float magnitude = std::abs(value);
			if (magnitude <= deadzone) return 0.0f;
			return std::copysign((magnitude - deadzone) / (1.0f - deadzone), value);
		}

		glm::vec2 apply_radial_deadzone(glm::vec2 value, float deadzone)
		{
			const float length = glm::length(value);
			if (length <= deadzone) return glm::vec2(0.0f);
			return value / length * ((length - deadzone) / (1.0f - deadzone));
		}

		bool uses_consumed(const Binding& binding, const std::unordered_set<uint32_t>& consumed)
		{
			for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
			{
				if (consumed.contains(control_key(binding.Parts[part]))) return true;
			}
			return false;
		}

		BindingResult evaluate_binding(const Binding& binding, const InputState& input)
		{
			BindingResult result;
			const float sign = binding.Invert ? -1.0f : 1.0f;

			switch (binding.BindingKind)
			{
			case Binding::Kind::Control:
			{
				const InputControl& control = binding.Parts[0];
				result.Value = apply_deadzone(input.value(control), binding.Deadzone) * binding.Scale * sign;
				result.RawPressed = input.pressed(control);
				result.RawReleased = input.released(control);
				break;
			}
			case Binding::Kind::Composite1D:
			{
				const float negative = std::abs(input.value(binding.Parts[0]));
				const float positive = std::abs(input.value(binding.Parts[1]));
				result.Value = apply_deadzone(positive - negative, binding.Deadzone) * binding.Scale * sign;
				break;
			}
			case Binding::Kind::Composite2D:
			{
				const float up = std::abs(input.value(binding.Parts[0]));
				const float down = std::abs(input.value(binding.Parts[1]));
				const float left = std::abs(input.value(binding.Parts[2]));
				const float right = std::abs(input.value(binding.Parts[3]));
				result.Vector = apply_radial_deadzone({ right - left, up - down }, binding.Deadzone) * binding.Scale * sign;
				break;
			}
			case Binding::Kind::Pair2D:
			{
				const glm::vec2 raw(input.value(binding.Parts[0]), input.value(binding.Parts[1]));
				result.Vector = apply_radial_deadzone(raw, binding.Deadzone) * binding.Scale * sign;
				break;
			}
			}
			return result;
		}

		ActionState evaluate_action(const Action& action, const ActionState& previous, const InputState& input, const std::unordered_set<uint32_t>& consumed)
		{
			const bool vector = action.Type == ActionType::Axis2D;

			ActionState state;
			float best = 0.0f;
			bool rawPressed = false;
			bool rawReleased = false;
			for (const Binding& binding : action.Bindings)
			{
				if (uses_consumed(binding, consumed)) continue;

				const BindingResult result = evaluate_binding(binding, input);
				rawPressed = rawPressed || result.RawPressed;
				rawReleased = rawReleased || result.RawReleased;

				const float magnitude = vector ? glm::length(result.Vector) : std::abs(result.Value);
				if (magnitude > best)
				{
					best = magnitude;
					state.Value = result.Value;
					state.Vector = result.Vector;
				}
			}

			if (vector)
			{
				if (best > 1.0f) state.Vector /= best;
				state.Value = glm::length(state.Vector);
				best = state.Value;
			}
			else if (action.Type == ActionType::Button)
			{
				state.Value = best;
			}

			state.Down = best >= ActionSystem::PRESS_THRESHOLD;
			// A press and release between two frames leaves Down false but still shows both edges.
			state.Pressed = (state.Down && !previous.Down) || rawPressed;
			state.Released = (!state.Down && previous.Down) || rawReleased;
			return state;
		}
	}

	int ActionSystem::map_index(std::string_view name) const
	{
		for (size_t i = 0; i < m_Maps.size(); i++)
		{
			if (m_Maps[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	void ActionSystem::set_maps(std::vector<ActionMap> maps)
	{
		m_Maps = std::move(maps);
		m_States.assign(m_Maps.size(), {});
		for (size_t i = 0; i < m_Maps.size(); i++)
		{
			m_States[i].assign(m_Maps[i].Actions.size(), ActionState{});
		}

		std::erase_if(m_Stack, [this](const std::string& name) { return map_index(name) < 0; });
	}

	const ActionMap* ActionSystem::find_map(std::string_view name) const
	{
		const int index = map_index(name);
		return index < 0 ? nullptr : &m_Maps[index];
	}

	void ActionSystem::push_map(std::string_view name)
	{
		if (map_index(name) < 0)
		{
			WI_INPUT_LOG(Warning, "push_map: there is no input map '{0}'", name);
			return;
		}

		pop_map(name);
		m_Stack.emplace_back(name);
	}

	void ActionSystem::pop_map(std::string_view name)
	{
		std::erase_if(m_Stack, [name](const std::string& entry) { return entry == name; });
	}

	bool ActionSystem::map_active(std::string_view name) const
	{
		return std::find(m_Stack.begin(), m_Stack.end(), name) != m_Stack.end();
	}

	std::vector<std::string> ActionSystem::active_maps() const
	{
		return std::vector<std::string>(m_Stack.rbegin(), m_Stack.rend());
	}

	void ActionSystem::evaluate(const InputState& input)
	{
		// Maps off the stack read zero, and their edges start over when they come back.
		for (size_t i = 0; i < m_Maps.size(); i++)
		{
			if (!map_active(m_Maps[i].Name))
			{
				std::fill(m_States[i].begin(), m_States[i].end(), ActionState{});
			}
		}

		// Top of the stack first, so a consuming map hides its controls from the maps below it.
		std::unordered_set<uint32_t> consumed;
		for (auto it = m_Stack.rbegin(); it != m_Stack.rend(); ++it)
		{
			const int index = map_index(*it);
			if (index < 0) continue;

			const ActionMap& map = m_Maps[index];
			for (size_t a = 0; a < map.Actions.size(); a++)
			{
				m_States[index][a] = evaluate_action(map.Actions[a], m_States[index][a], input, consumed);
			}

			if (map.Consume)
			{
				for (const Action& action : map.Actions)
				{
					for (const Binding& binding : action.Bindings)
					{
						for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
						{
							consumed.insert(control_key(binding.Parts[part]));
						}
					}
				}
			}
		}
	}

	ActionState ActionSystem::state(std::string_view map, std::string_view action) const
	{
		const int index = map_index(map);
		if (index < 0 || !map_active(map)) return {};

		const std::vector<Action>& actions = m_Maps[index].Actions;
		for (size_t a = 0; a < actions.size(); a++)
		{
			if (actions[a].Name == action) return m_States[index][a];
		}
		return {};
	}

	bool ActionSystem::has_action(std::string_view map, std::string_view action) const
	{
		const ActionMap* found = find_map(map);
		if (!found) return false;

		for (const Action& candidate : found->Actions)
		{
			if (candidate.Name == action) return true;
		}
		return false;
	}
}
