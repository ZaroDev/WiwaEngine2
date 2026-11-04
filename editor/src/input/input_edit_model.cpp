#include <editor_common.h>
#include "input_edit_model.h"

#include <cmath>

namespace wiwa
{
	InputEditModel::InputEditModel(InputMapData data) : m_Data(std::move(data)) {}

	void InputEditModel::commit(Snapshot before)
	{
		m_Undo.push_back(std::move(before));
		while (m_Undo.size() > MAX_UNDO_STEPS)
		{
			m_Undo.pop_front();
		}
		m_Redo.clear();
		m_Revision = ++m_RevisionCounter;
	}

	void InputEditModel::restore(Snapshot state)
	{
		m_Data = std::move(state.Data);
		m_Revision = state.Revision;
	}

	void InputEditModel::undo()
	{
		if (m_Undo.empty())
		{
			return;
		}
		Snapshot previous = std::move(m_Undo.back());
		m_Undo.pop_back();
		m_Redo.push_back(snapshot());
		restore(std::move(previous));
	}

	void InputEditModel::redo()
	{
		if (m_Redo.empty())
		{
			return;
		}
		Snapshot next = std::move(m_Redo.back());
		m_Redo.pop_back();
		m_Undo.push_back(snapshot());
		restore(std::move(next));
	}

	bool InputEditModel::valid_name(const std::string& name)
	{
		return !name.empty() && name.front() != ' ' && name.back() != ' ';
	}

	Action* InputEditModel::action_at(size_t map, size_t action)
	{
		if (map >= m_Data.Maps.size() || action >= m_Data.Maps[map].Actions.size())
		{
			return nullptr;
		}
		return &m_Data.Maps[map].Actions[action];
	}

	Binding* InputEditModel::binding_at(size_t map, size_t action, size_t binding)
	{
		Action* found = action_at(map, action);
		return found != nullptr && binding < found->Bindings.size() ? &found->Bindings[binding] : nullptr;
	}

	size_t InputEditModel::add_map(const std::string& name)
	{
		if (!valid_name(name) || std::any_of(m_Data.Maps.begin(), m_Data.Maps.end(), [&](const ActionMap& map) { return map.Name == name; }))
		{
			return SIZE_MAX;
		}

		Snapshot before = snapshot();
		ActionMap map;
		map.Name = name;
		m_Data.Maps.push_back(std::move(map));
		commit(std::move(before));
		return m_Data.Maps.size() - 1;
	}

	bool InputEditModel::remove_map(size_t map)
	{
		if (map >= m_Data.Maps.size())
		{
			return false;
		}

		Snapshot before = snapshot();
		m_Data.Maps.erase(m_Data.Maps.begin() + map);
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::rename_map(size_t map, const std::string& name)
	{
		if (map >= m_Data.Maps.size() || !valid_name(name))
		{
			return false;
		}
		if (m_Data.Maps[map].Name == name)
		{
			return true;
		}
		for (size_t i = 0; i < m_Data.Maps.size(); i++)
		{
			if (i != map && m_Data.Maps[i].Name == name)
			{
				return false;
			}
		}

		Snapshot before = snapshot();
		m_Data.Maps[map].Name = name;
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::set_consume(size_t map, bool consume)
	{
		if (map >= m_Data.Maps.size())
		{
			return false;
		}
		if (m_Data.Maps[map].Consume == consume)
		{
			return true;
		}

		Snapshot before = snapshot();
		m_Data.Maps[map].Consume = consume;
		commit(std::move(before));
		return true;
	}

	size_t InputEditModel::add_action(size_t map, const std::string& name, ActionType type)
	{
		if (map >= m_Data.Maps.size() || !valid_name(name))
		{
			return SIZE_MAX;
		}
		std::vector<Action>& actions = m_Data.Maps[map].Actions;
		if (std::any_of(actions.begin(), actions.end(), [&](const Action& action) { return action.Name == name; }))
		{
			return SIZE_MAX;
		}

		Snapshot before = snapshot();
		Action action;
		action.Name = name;
		action.Type = type;
		actions.push_back(std::move(action));
		commit(std::move(before));
		return actions.size() - 1;
	}

	bool InputEditModel::remove_action(size_t map, size_t action)
	{
		if (action_at(map, action) == nullptr)
		{
			return false;
		}

		Snapshot before = snapshot();
		std::vector<Action>& actions = m_Data.Maps[map].Actions;
		actions.erase(actions.begin() + action);
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::rename_action(size_t map, size_t action, const std::string& name)
	{
		Action* found = action_at(map, action);
		if (found == nullptr || !valid_name(name))
		{
			return false;
		}
		if (found->Name == name)
		{
			return true;
		}
		const std::vector<Action>& actions = m_Data.Maps[map].Actions;
		for (size_t i = 0; i < actions.size(); i++)
		{
			if (i != action && actions[i].Name == name)
			{
				return false;
			}
		}

		Snapshot before = snapshot();
		found->Name = name;
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::set_action_type(size_t map, size_t action, ActionType type)
	{
		Action* found = action_at(map, action);
		if (found == nullptr)
		{
			return false;
		}
		if (found->Type == type)
		{
			return true;
		}
		for (const Binding& binding : found->Bindings)
		{
			if (!action_accepts(type, binding.BindingKind))
			{
				return false;
			}
		}

		Snapshot before = snapshot();
		found->Type = type;
		commit(std::move(before));
		return true;
	}

	size_t InputEditModel::add_binding(size_t map, size_t action, const Binding& binding)
	{
		Action* found = action_at(map, action);
		if (found == nullptr || !action_accepts(found->Type, binding.BindingKind) || !std::isfinite(binding.Scale) || !(binding.Deadzone >= 0.0f && binding.Deadzone < 1.0f))
		{
			return SIZE_MAX;
		}
		for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
		{
			if (control_name(binding.Parts[part]).empty())
			{
				return SIZE_MAX;
			}
		}

		Snapshot before = snapshot();
		found->Bindings.push_back(binding);
		commit(std::move(before));
		return found->Bindings.size() - 1;
	}

	bool InputEditModel::remove_binding(size_t map, size_t action, size_t binding)
	{
		if (binding_at(map, action, binding) == nullptr)
		{
			return false;
		}

		Snapshot before = snapshot();
		std::vector<Binding>& bindings = m_Data.Maps[map].Actions[action].Bindings;
		bindings.erase(bindings.begin() + binding);
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::set_binding_part(size_t map, size_t action, size_t binding, size_t part, const InputControl& control)
	{
		Binding* found = binding_at(map, action, binding);
		if (found == nullptr || part >= binding_part_count(found->BindingKind) || control_name(control).empty())
		{
			return false;
		}
		if (found->Parts[part] == control)
		{
			return true;
		}

		Snapshot before = snapshot();
		found->Parts[part] = control;
		commit(std::move(before));
		return true;
	}

	bool InputEditModel::set_binding_params(size_t map, size_t action, size_t binding, float scale, float deadzone, bool invert)
	{
		Binding* found = binding_at(map, action, binding);
		if (found == nullptr || !std::isfinite(scale) || !(deadzone >= 0.0f && deadzone < 1.0f))
		{
			return false;
		}
		if (found->Scale == scale && found->Deadzone == deadzone && found->Invert == invert)
		{
			return true;
		}

		Snapshot before = snapshot();
		found->Scale = scale;
		found->Deadzone = deadzone;
		found->Invert = invert;
		commit(std::move(before));
		return true;
	}
}
