#include <editor_common.h>
#include "shortcuts.h"

void ShortcutRegistry::add(ShortcutAction action)
{
	m_Actions.push_back(std::move(action));
}

const ShortcutAction* ShortcutRegistry::find(const std::string& id) const
{
	for (const ShortcutAction& action : m_Actions)
	{
		if (action.Id == id)
		{
			return &action;
		}
	}
	return nullptr;
}

bool ShortcutRegistry::run(const std::string& id) const
{
	const ShortcutAction* action = find(id);
	if (action == nullptr || !action->Run || !enabled(*action))
	{
		return false;
	}
	action->Run();
	return true;
}

void ShortcutRegistry::poll() const
{
	if (ImGui::GetIO().WantTextInput)
	{
		return;
	}
	for (const ShortcutAction& action : m_Actions)
	{
		if (action.Chord != 0 && ImGui::IsKeyChordPressed(action.Chord) && enabled(action))
		{
			if (action.Run)
			{
				action.Run();
			}
			return; // one action per frame: Ctrl+Z and Ctrl+Shift+Z share a key
		}
	}
}

std::string ShortcutRegistry::chord_text(ImGuiKeyChord chord)
{
	if (chord == 0)
	{
		return {};
	}
	std::string text;
	if (chord & ImGuiMod_Ctrl) { text += "Ctrl+"; }
	if (chord & ImGuiMod_Shift) { text += "Shift+"; }
	if (chord & ImGuiMod_Alt) { text += "Alt+"; }
	const ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
	text += ImGui::GetKeyName(key);
	return text;
}

void ShortcutRegistry::draw_cheatsheet(bool& open) const
{
	if (!open)
	{
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_Appearing);
	if (ImGui::Begin("Keyboard shortcuts", &open))
	{
		if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			for (const ShortcutAction& action : m_Actions)
			{
				if (action.Chord == 0)
				{
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(action.Label.c_str());
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", chord_text(action.Chord).c_str());
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}
