#pragma once

#include <cstddef>
#include <deque>
#include <utility>

#include <math/math.h>

namespace wiwa
{
	// Snapshot undo for an asset editor's document: the state is small for names, sockets and slots, so each step keeps a copy; a clip can be big, so
	// a step also says how many bytes it holds and the oldest steps go when the total is over MAX_UNDO_BYTES. A call that changes nothing makes no step.
	// dirty() compares revisions, so undoing back to what was saved is clean again.
	template<typename State>
	class EditHistory
	{
	public:
		static constexpr size_t MAX_UNDO_STEPS = 100;
		static constexpr size_t MAX_UNDO_BYTES = 64ull * 1024 * 1024;

		// Call before changing the state: push(before) after the change is known to be a real one. bytes: the size of `before` (0: not counted).
		void push(State before, size_t bytes = 0)
		{
			m_Undo.push_back(Step{ std::move(before), m_Revision, bytes });
			m_UndoBytes += bytes;
			m_Redo.clear();
			m_RedoBytes = 0;
			m_Revision = m_NextRevision++;
			while (m_Undo.size() > 1 && (m_Undo.size() > MAX_UNDO_STEPS || m_UndoBytes + m_RedoBytes > MAX_UNDO_BYTES))
			{
				m_UndoBytes -= m_Undo.front().Bytes;
				m_Undo.pop_front();
			}
		}

		bool can_undo() const { return !m_Undo.empty(); }
		bool can_redo() const { return !m_Redo.empty(); }

		// Swap the current state for the previous (next) one; returns it for the caller to put in place. bytes: the size of `current`.
		State undo(State current, size_t bytes = 0)
		{
			Step step = std::move(m_Undo.back());
			m_Undo.pop_back();
			m_UndoBytes -= step.Bytes;
			m_Redo.push_back(Step{ std::move(current), m_Revision, bytes });
			m_RedoBytes += bytes;
			m_Revision = step.Revision;
			return std::move(step.Value);
		}

		State redo(State current, size_t bytes = 0)
		{
			Step step = std::move(m_Redo.back());
			m_Redo.pop_back();
			m_RedoBytes -= step.Bytes;
			m_Undo.push_back(Step{ std::move(current), m_Revision, bytes });
			m_UndoBytes += bytes;
			m_Revision = step.Revision;
			return std::move(step.Value);
		}

		bool dirty() const { return m_Revision != m_SavedRevision; }
		void mark_saved() { m_SavedRevision = m_Revision; }

	private:
		struct Step
		{
			State Value{};
			u64 Revision = 0;
			size_t Bytes = 0;
		};

		std::deque<Step> m_Undo{};
		std::deque<Step> m_Redo{};
		size_t m_UndoBytes = 0;
		size_t m_RedoBytes = 0;
		u64 m_Revision = 0;
		u64 m_SavedRevision = 0;
		u64 m_NextRevision = 1;
	};
}
