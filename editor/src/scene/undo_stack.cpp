#include <editor_common.h>
#include "undo_stack.h"

namespace wiwa
{
	void UndoStack::clear()
	{
		m_Undo.clear();
		m_Redo.clear();
		m_Bytes = 0;
	}

	void UndoStack::push(std::string label, std::string before)
	{
		for (const Step& step : m_Redo)
		{
			m_Bytes -= step.State.size();
		}
		m_Redo.clear();
		m_Bytes += before.size();
		m_Undo.push_back(Step{ std::move(label), std::move(before) });
		trim();
	}

	void UndoStack::trim()
	{
		while (m_Undo.size() > 1 && (m_Undo.size() > MAX_STEPS || m_Bytes > MAX_BYTES))
		{
			m_Bytes -= m_Undo.front().State.size();
			m_Undo.pop_front();
		}
	}

	const std::string& UndoStack::undo_label() const
	{
		static const std::string none;
		return m_Undo.empty() ? none : m_Undo.back().Label;
	}

	const std::string& UndoStack::redo_label() const
	{
		static const std::string none;
		return m_Redo.empty() ? none : m_Redo.back().Label;
	}

	std::optional<std::string> UndoStack::undo(const std::string& current)
	{
		if (m_Undo.empty())
		{
			return std::nullopt;
		}
		Step step = std::move(m_Undo.back());
		m_Undo.pop_back();
		std::string restored = std::move(step.State);
		m_Bytes -= restored.size();
		m_Bytes += current.size();
		m_Redo.push_back(Step{ std::move(step.Label), current });
		return restored;
	}

	std::optional<std::string> UndoStack::redo(const std::string& current)
	{
		if (m_Redo.empty())
		{
			return std::nullopt;
		}
		Step step = std::move(m_Redo.back());
		m_Redo.pop_back();
		std::string restored = std::move(step.State);
		m_Bytes -= restored.size();
		m_Bytes += current.size();
		m_Undo.push_back(Step{ std::move(step.Label), current });
		trim();
		return restored;
	}

	std::vector<std::string> UndoStack::undo_labels() const
	{
		std::vector<std::string> labels;
		for (const Step& step : m_Undo)
		{
			labels.push_back(step.Label);
		}
		return labels;
	}

	std::vector<std::string> UndoStack::redo_labels() const
	{
		std::vector<std::string> labels;
		for (auto it = m_Redo.rbegin(); it != m_Redo.rend(); ++it)
		{
			labels.push_back(it->Label);
		}
		return labels;
	}
}
