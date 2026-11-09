#include <editor_common.h>
#include "history_panel.h"

#include <imgui.h>

void HistoryPanel::on_draw()
{
	if (!m_Open)
	{
		return;
	}

	size_t jumpTo = SIZE_MAX;
	if (ImGui::Begin(get_name(), &m_Open))
	{
		const wiwa::UndoStack& history = m_Editor.history();
		const std::vector<std::string> undo = history.undo_labels();
		const std::vector<std::string> redo = history.redo_labels();
		if (!m_Editor.can_edit_history())
		{
			ImGui::TextDisabled("History is not available now (playing, or the scene is loading)");
		}

		// target = the number of steps applied
		if (ImGui::Selectable("Opened", undo.empty() && redo.empty() ? true : undo.empty()))
		{
			jumpTo = 0;
		}
		for (size_t i = 0; i < undo.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(undo[i].c_str(), i + 1 == undo.size()))
			{
				jumpTo = i + 1;
			}
			ImGui::PopID();
		}
		for (size_t i = 0; i < redo.size(); i++)
		{
			ImGui::PushID(static_cast<int>(undo.size() + i));
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			if (ImGui::Selectable(redo[i].c_str(), false))
			{
				jumpTo = undo.size() + i + 1;
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}
	ImGui::End();

	if (jumpTo != SIZE_MAX)
	{
		m_Editor.jump_history(jumpTo);
	}
}
