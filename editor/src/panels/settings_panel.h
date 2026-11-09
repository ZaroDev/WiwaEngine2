#pragma once

#include <string>

#include <imgui.h>

#include "panel.h"

#include "../settings/editor_section.h"
#include "../settings/input_section.h"
#include "../settings/physics_section.h"
#include "../settings/project_section.h"
#include "../settings/renderer_section.h"

// The "Settings" panel: every project-wide value in one place, in pages on the left (Project, Launch, Performance, Renderer, Physics, Input, Editor).
// The search box keeps only the rows whose label contains the text, across all pages. Each change is saved at once and applied by the page's
// section (ProjectSection...); this class only draws rows. Closed until opened from the Window menu. Main thread, between the ImGui frame's
// begin and end.
class SettingsPanel final : public Panel
{
public:
	SettingsPanel(Ref<ProjectSection> project, Ref<RendererSection> renderer, Ref<PhysicsSection> physics, Ref<InputSection> input, Ref<EditorSection> editor);

	virtual void on_draw() override;

private:
	enum class Page : int
	{
		Project,
		Launch,
		Performance,
		Renderer,
		Physics,
		Input,
		Editor,
		Count,
	};

	static const char* page_name(Page page);

	void draw_page(Page page);
	void project_rows();
	void launch_rows();
	void performance_rows();
	void renderer_rows();
	void physics_rows();
	void input_rows();
	void editor_rows();

	// A row: drawn only if its label matches the search; the page title is drawn before the first row that shows while searching.
	template<typename Draw>
	void field(const char* label, Draw&& draw);
	// A "(restart needed)" tag for values a running game or the editor does not take back.
	void restart_note() const;
	// A small button that puts a value back to its default; only shown when it differs.
	template<typename T>
	bool reset_button(const char* id, T& value, const T& defaultValue);
	bool matches(const char* text) const;
	void show_error(const std::string& error) const;

	Ref<ProjectSection> m_Project;
	Ref<RendererSection> m_Renderer;
	Ref<PhysicsSection> m_Physics;
	Ref<InputSection> m_Input;
	Ref<EditorSection> m_Editor;

	Page m_Page = Page::Project;
	char m_Search[128] = "";
	std::string m_Needle{};       // the search, lower case
	bool m_TitlePending = false;  // searching and the page title has not been drawn yet
	const char* m_Title = "";
};

template<typename Draw>
void SettingsPanel::field(const char* label, Draw&& draw)
{
	if (!matches(label))
	{
		return;
	}
	if (m_TitlePending)
	{
		ImGui::SeparatorText(m_Title);
		m_TitlePending = false;
	}
	ImGui::PushID(label);
	draw();
	ImGui::PopID();
}

template<typename T>
bool SettingsPanel::reset_button(const char* id, T& value, const T& defaultValue)
{
	if (value == defaultValue)
	{
		return false;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(id))
	{
		value = defaultValue;
		return true;
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Back to the default");
	}
	return false;
}
