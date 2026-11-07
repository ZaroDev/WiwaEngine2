#include <editor_common.h>
#include "command_palette.h"

#include <imgui.h>

namespace
{
	constexpr size_t MAX_SHOWN = 12;

	char lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}
}

int CommandPalette::score(const std::string& query, const std::string& text)
{
	if (query.empty())
	{
		return 1;
	}

	// every character of the query has to appear in order; consecutive and word-start matches count more
	int total = 1;
	size_t at = 0;
	bool previousMatched = false;
	for (const char wanted : query)
	{
		bool found = false;
		for (; at < text.size(); at++)
		{
			if (lower(text[at]) == lower(wanted))
			{
				total += 1 + (previousMatched ? 4 : 0) + ((at == 0 || text[at - 1] == ' ' || text[at - 1] == '/' || text[at - 1] == '_') ? 3 : 0);
				previousMatched = true;
				found = true;
				at++;
				break;
			}
			previousMatched = false;
		}
		if (!found)
		{
			return 0;
		}
	}
	return total - static_cast<int>(text.size() / 8); // shorter texts first among equals
}

void CommandPalette::open(std::vector<Item> items)
{
	m_Items = std::move(items);
	m_Open = true;
	m_Appearing = true;
	m_Query[0] = '\0';
	m_Selected = 0;
}

void CommandPalette::draw()
{
	if (!m_Open)
	{
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + viewport->Size.y * 0.15f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, viewport->Size.x - 32.0f), 0.0f));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
	if (!ImGui::Begin("##command_palette", &m_Open, flags))
	{
		ImGui::End();
		return;
	}

	if (m_Appearing)
	{
		ImGui::SetKeyboardFocusHere();
		m_Appearing = false;
	}
	ImGui::SetNextItemWidth(-1.0f);
	const bool edited = ImGui::InputTextWithHint("##palette_query", "Type to search actions, panels, entities and assets...", m_Query, sizeof(m_Query));
	if (edited)
	{
		m_Selected = 0;
	}

	// the best matches
	struct Match
	{
		size_t Index;
		int Score;
	};
	std::vector<Match> matches;
	const std::string query = m_Query;
	for (size_t i = 0; i < m_Items.size(); i++)
	{
		if (const int s = score(query, m_Items[i].Label))
		{
			matches.push_back({ i, s });
		}
	}
	std::stable_sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) { return a.Score > b.Score; });
	if (matches.size() > MAX_SHOWN)
	{
		matches.resize(MAX_SHOWN);
	}

	const int count = static_cast<int>(matches.size());
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && count > 0)
	{
		m_Selected = (m_Selected + 1) % count;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && count > 0)
	{
		m_Selected = (m_Selected + count - 1) % count;
	}
	m_Selected = std::clamp(m_Selected, 0, std::max(count - 1, 0));

	int chosen = -1;
	ImGui::Separator();
	if (count == 0)
	{
		ImGui::TextDisabled("No match");
	}
	for (int row = 0; row < count; row++)
	{
		const Item& item = m_Items[matches[static_cast<size_t>(row)].Index];
		ImGui::PushID(row);
		if (ImGui::Selectable(("##row"), row == m_Selected, ImGuiSelectableFlags_SpanAllColumns))
		{
			chosen = row;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%-7s", item.Group.c_str());
		ImGui::SameLine();
		ImGui::TextUnformatted(item.Label.c_str());
		if (!item.Detail.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("  %s", item.Detail.c_str());
		}
		ImGui::PopID();
	}

	if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && count > 0)
	{
		chosen = m_Selected;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !m_Appearing && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
	{
		m_Open = false;
	}

	std::function<void()> run;
	if (chosen >= 0)
	{
		run = m_Items[matches[static_cast<size_t>(chosen)].Index].Run;
		m_Open = false;
	}
	ImGui::End();

	if (run)
	{
		run(); // after the window, so that the action can open popups or move panels
	}
}
