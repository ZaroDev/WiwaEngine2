#pragma once

#include <string>
#include "panel.h"

#include <imgui.h>

#include <scene/scene_editor.h>

namespace wiwa
{
	class RenderModule;
}

// The "World" panel: everything that decides how the scene looks, in collapsible sections (environment, lighting, exposure, tonemap, bloom,
// ambient occlusion, temporal AA, motion blur and the scene's scripts). The search box at the top keeps only the rows whose label (or
// group, or section) contains the text; a section with no such row is hidden and the others are opened. The scene's settings are saved
// with the scene; the renderer's quality is the project's and is in the Settings panel. Main thread, between the ImGui frame's begin and end.
class WorldPanel final : public Panel
{
public:
	explicit WorldPanel(wiwa::SceneEditor& editor) : Panel("World", PanelCategory::Window), m_Editor(editor) {}

	virtual void on_draw() override;

private:
	// A section draws its rows through field(), group() and note() only, so that it can be run twice: once to find out whether anything in
	// it matches the search (nothing is drawn), and once to draw.
	template<typename Draw>
	void section(const char* title, bool openByDefault, Draw&& rows);
	template<typename Draw>
	void field(const char* label, Draw&& draw);
	void group(const char* name);
	void note(const char* text);

	bool matches(const char* text) const;

	void environment_rows(wiwa::SceneSettings& settings);
	void lighting_rows(wiwa::SceneSettings& settings);
	void exposure_rows(wiwa::SceneSettings& settings);
	void tonemap_rows(wiwa::SceneSettings& settings);
	void bloom_rows(wiwa::SceneSettings& settings);
	void ao_rows(wiwa::SceneSettings& settings);
	void taa_rows(wiwa::SceneSettings& settings, wiwa::RenderModule* render);
	void motion_blur_rows(wiwa::SceneSettings& settings, wiwa::RenderModule* render);
	void scripts_rows(wiwa::SceneSettings& settings);

	wiwa::SceneEditor& m_Editor;

	char m_Search[128] = "";
	std::string m_Needle{};      // the search, lower case
	bool m_Filtering = false;
	bool m_Collecting = false;   // the first pass over a section: rows are only tested
	bool m_SectionHit = false;   // a row of the section being collected matches
	bool m_TitleHit = false;     // the title of the section being drawn matches: all its rows show
	bool m_LastShown = false;    // the last row was drawn (a note follows its row)
	std::string m_Group{};
	bool m_GroupPending = false; // the group's separator has not been drawn yet
	bool m_AnySection = false;

	char m_Path[260] = "Assets/environments/";
	std::string m_PathStatus{};
	wiwa::AssetGuid m_ShownGuid{}; // the asset the status line was last filled for
	bool m_ShowVelocity = false;

	char m_ScriptPath[260] = "Assets/scripts/";
	std::string m_ScriptStatus{};
};

template<typename Draw>
void WorldPanel::section(const char* title, bool openByDefault, Draw&& rows)
{
	m_TitleHit = false;
	if (m_Filtering)
	{
		m_Collecting = true;
		m_SectionHit = false;
		m_Group.clear();
		rows();
		m_Collecting = false;

		m_TitleHit = matches(title);
		if (!m_SectionHit && !m_TitleHit)
		{
			return;
		}
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	else if (openByDefault)
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	}

	m_AnySection = true;
	if (ImGui::CollapsingHeader(title))
	{
		ImGui::PushID(title);
		m_Group.clear();
		m_GroupPending = false;
		m_LastShown = false;
		rows();
		ImGui::PopID();
	}
}

template<typename Draw>
void WorldPanel::field(const char* label, Draw&& draw)
{
	const bool hit = matches(label) || (!m_Group.empty() && matches(m_Group.c_str()));
	if (m_Collecting)
	{
		m_SectionHit = m_SectionHit || hit;
		return;
	}

	m_LastShown = !m_Filtering || m_TitleHit || hit;
	if (!m_LastShown)
	{
		return;
	}
	if (m_GroupPending)
	{
		ImGui::SeparatorText(m_Group.c_str());
		m_GroupPending = false;
	}
	draw();
}
