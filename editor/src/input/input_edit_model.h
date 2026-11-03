#pragma once

#include <deque>
#include <string>
#include <vector>

#include <input/action_types.h>

namespace wiwa
{
	// The document the Input panel edits: action maps and their history. No ImGui, so every rule of editing is tested without a window.
	// Every call that changes the document is one undo step; a call that is refused (it returns false, or SIZE_MAX for an index) changes
	// nothing and makes no step, and neither does setting a value to what it already is (that returns true). Maps, actions and bindings
	// are named by their index. Names are not empty, have no space at either end and are unique among their siblings.
	class InputEditModel
	{
	public:
		static constexpr size_t MAX_UNDO_STEPS = 100;

		explicit InputEditModel(InputMapData data);

		const InputMapData& data() const { return m_Data; }

		// The index of the new map / action / binding (last in its list), SIZE_MAX if refused.
		size_t add_map(const std::string& name);
		bool remove_map(size_t map);
		bool rename_map(size_t map, const std::string& name);
		bool set_consume(size_t map, bool consume);

		size_t add_action(size_t map, const std::string& name, ActionType type);
		bool remove_action(size_t map, size_t action);
		bool rename_action(size_t map, size_t action, const std::string& name);
		// Refused if a binding the action has would be invalid for the new type: the caller removes those first.
		bool set_action_type(size_t map, size_t action, ActionType type);

		// Refused if the action's type cannot use the binding's kind, or the binding's values are not usable.
		size_t add_binding(size_t map, size_t action, const Binding& binding);
		bool remove_binding(size_t map, size_t action, size_t binding);
		// `part` indexes the parts of the binding's kind (binding_part_name).
		bool set_binding_part(size_t map, size_t action, size_t binding, size_t part, const InputControl& control);
		// The deadzone is in [0, 1), the scale is finite.
		bool set_binding_params(size_t map, size_t action, size_t binding, float scale, float deadzone, bool invert);

		void undo();
		void redo();
		bool can_undo() const { return !m_Undo.empty(); }
		bool can_redo() const { return !m_Redo.empty(); }

		// Whether the document differs from what was last saved (undoing back to it is clean again).
		bool dirty() const { return m_Revision != m_SavedRevision; }
		void mark_saved() { m_SavedRevision = m_Revision; }

	private:
		struct Snapshot
		{
			InputMapData Data{};
			u64 Revision = 0;
		};

		Snapshot snapshot() const { return Snapshot{ m_Data, m_Revision }; }
		// Records the state before a change as an undo step.
		void commit(Snapshot before);
		void restore(Snapshot state);

		static bool valid_name(const std::string& name);
		Action* action_at(size_t map, size_t action);
		Binding* binding_at(size_t map, size_t action, size_t binding);

		InputMapData m_Data{};
		std::deque<Snapshot> m_Undo{};
		std::vector<Snapshot> m_Redo{};
		u64 m_Revision = 0;
		u64 m_SavedRevision = 0;
		u64 m_RevisionCounter = 0;
	};
}
