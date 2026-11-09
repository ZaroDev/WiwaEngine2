#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace wiwa
{
	// Undo and redo of the scene document as whole-document snapshots (compact JSON text of what Save writes). Every step keeps the state
	// the document had before the edit and a label for the Edit menu and the History panel. Main thread.
	class UndoStack
	{
	public:
		static constexpr size_t MAX_STEPS = 100;
		static constexpr size_t MAX_BYTES = 64u * 1024u * 1024u;

		void clear();

		// A new edit: before is the document as it was. Drops the redo steps and the oldest steps past the limits.
		void push(std::string label, std::string before);

		bool can_undo() const { return !m_Undo.empty(); }
		bool can_redo() const { return !m_Redo.empty(); }
		const std::string& undo_label() const;
		const std::string& redo_label() const;

		// The state to restore; current is the state being left (it becomes the redo or undo step). Empty when there is nothing to do.
		std::optional<std::string> undo(const std::string& current);
		std::optional<std::string> redo(const std::string& current);

		// Oldest first; the redo labels are the ones that can be redone, nearest first.
		std::vector<std::string> undo_labels() const;
		std::vector<std::string> redo_labels() const;

	private:
		struct Step
		{
			std::string Label{};
			std::string State{};
		};

		void trim();

		std::deque<Step> m_Undo{};
		std::deque<Step> m_Redo{};
		size_t m_Bytes = 0;
	};
}
