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

				// A Button is pressed in one direction: a stick pushed up presses a binding as it is, pushed down it needs Invert.
				const float magnitude = vector ? glm::length(result.Vector) : action.Type == ActionType::Button ? std::max(result.Value, 0.0f) : std::abs(result.Value);
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
		m_Defaults = maps;
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
		if (m_Listen)
		{
			update_listen(input);
			for (std::vector<ActionState>& states : m_States)
			{
				std::fill(states.begin(), states.end(), ActionState{});
			}
			return;
		}

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

	Action* ActionSystem::find_action(std::string_view map, std::string_view name)
	{
		const int index = map_index(map);
		if (index < 0) return nullptr;

		for (Action& candidate : m_Maps[index].Actions)
		{
			if (candidate.Name == name) return &candidate;
		}
		return nullptr;
	}

	const Action* ActionSystem::find_action(std::string_view map, std::string_view name) const
	{
		return const_cast<ActionSystem*>(this)->find_action(map, name);
	}

	const Action* ActionSystem::find_default(std::string_view map, std::string_view name) const
	{
		for (const ActionMap& defaults : m_Defaults)
		{
			if (defaults.Name != map) continue;
			for (const Action& candidate : defaults.Actions)
			{
				if (candidate.Name == name) return &candidate;
			}
		}
		return nullptr;
	}

	const Action* ActionSystem::action(std::string_view map, std::string_view name) const
	{
		return find_action(map, name);
	}

	bool ActionSystem::rebind(std::string_view map, std::string_view action, size_t binding, const InputControl& control, std::string_view part)
	{
		Action* found = find_action(map, action);
		if (!found || binding >= found->Bindings.size() || control_name(control).empty()) return false;

		Binding& target = found->Bindings[binding];
		size_t partIndex = 0;
		if (!part.empty())
		{
			partIndex = binding_part_count(target.BindingKind);
			for (size_t i = 0; i < binding_part_count(target.BindingKind); i++)
			{
				if (part == binding_part_name(target.BindingKind, i)) partIndex = i;
			}
			if (partIndex >= binding_part_count(target.BindingKind)) return false;
		}

		target.Parts[partIndex] = control;
		return true;
	}

	size_t ActionSystem::add_binding(std::string_view map, std::string_view action, const Binding& binding)
	{
		Action* found = find_action(map, action);
		if (!found || !action_accepts(found->Type, binding.BindingKind)) return SIZE_MAX;

		found->Bindings.push_back(binding);
		return found->Bindings.size() - 1;
	}

	bool ActionSystem::remove_binding(std::string_view map, std::string_view action, size_t binding)
	{
		Action* found = find_action(map, action);
		if (!found || binding >= found->Bindings.size()) return false;

		found->Bindings.erase(found->Bindings.begin() + binding);
		return true;
	}

	bool ActionSystem::reset_action(std::string_view map, std::string_view action)
	{
		Action* found = find_action(map, action);
		const Action* defaults = find_default(map, action);
		if (!found || !defaults) return false;

		found->Bindings = defaults->Bindings;
		return true;
	}

	void ActionSystem::reset_all()
	{
		// The defaults have the same maps and actions as the current set; only bindings were edited.
		m_Maps = m_Defaults;
	}

	std::vector<BindingConflict> ActionSystem::conflicts(std::string_view map, const InputControl& control, std::string_view exceptAction) const
	{
		std::vector<BindingConflict> result;
		const ActionMap* found = find_map(map);
		if (!found) return result;

		for (const Action& candidate : found->Actions)
		{
			if (!exceptAction.empty() && candidate.Name == exceptAction) continue;

			for (size_t b = 0; b < candidate.Bindings.size(); b++)
			{
				const Binding& binding = candidate.Bindings[b];
				for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
				{
					if (binding.Parts[part] == control)
					{
						result.push_back({ found->Name, candidate.Name, b });
						break;
					}
				}
			}
		}
		return result;
	}

	bool ActionSystem::modified(std::string_view map, std::string_view action) const
	{
		const Action* current = find_action(map, action);
		const Action* defaults = find_default(map, action);
		return current && defaults && current->Bindings != defaults->Bindings;
	}

	void ActionSystem::listen_for_control(std::function<void(const InputControl&)> callback)
	{
		m_Listen = std::move(callback);
	}

	void ActionSystem::update_listen(const InputState& input)
	{
		// Escape always cancels; nothing else is excluded.
		if (input.pressed(control(Key::Escape)))
		{
			m_Listen = nullptr;
			return;
		}

		for (const InputControl& candidate : all_controls())
		{
			bool hit = false;
			if (!control_is_axis(candidate))
			{
				hit = input.pressed(candidate);
			}
			else if (candidate.Device == InputDevice::Pad)
			{
				// Mouse axes are left out: moving the mouse would end the listening at once.
				hit = std::abs(input.value(candidate)) > PRESS_THRESHOLD;
			}

			if (hit)
			{
				// Listening ends before the callback runs, so the callback may start a new listen.
				auto callback = std::move(m_Listen);
				m_Listen = nullptr;
				callback(candidate);
				return;
			}
		}
	}

	bool ActionSystem::set_bindings(std::string_view map, std::string_view action, std::vector<Binding> bindings)
	{
		Action* found = find_action(map, action);
		if (!found) return false;

		for (const Binding& binding : bindings)
		{
			if (!action_accepts(found->Type, binding.BindingKind)) return false;
		}
		found->Bindings = std::move(bindings);
		return true;
	}

	void ActionSystem::add_maps(std::vector<ActionMap> maps)
	{
		for (ActionMap& map : maps)
		{
			if (map_index(map.Name) >= 0)
			{
				WI_INPUT_LOG(Warning, "add_maps: there is an input map '{0}' already", map.Name);
				continue;
			}
			m_Defaults.push_back(map);
			m_States.emplace_back(map.Actions.size());
			m_Maps.push_back(std::move(map));
		}
	}
}
