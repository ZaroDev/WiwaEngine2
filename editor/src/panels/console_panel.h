#pragma once
#include "panel.h"
#include <core/log.h>
#include "imgui.h"
#include <math/math.h>

class ConsolePanel final : public Panel
{
public:
	ConsolePanel();
	~ConsolePanel() override;

	void on_update() override;
	void on_draw() override;

private:
	void clear();
	void add_log(wiwa::LogLevel level, const char* fmt, ...) IM_FMTARGS(2);
private:


	ImGuiTextBuffer     m_Buf;
	ImGuiTextFilter     m_Filter;
	ImVector<wiwa::u32>       m_LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
	ImVector<wiwa::LogLevel>  m_LogLevels;
	bool                m_AutoScroll;  // Keep scrolling if already at the bottom.
	bool                m_ShowInfo = true;
	bool                m_ShowWarning = true;
	bool                m_ShowError = true;
	bool level_visible(wiwa::LogLevel level) const;

	ImVec4 m_LevelColors[wiwa::LogLevel::Last];
	wiwa::u32 m_InfoCount;
	wiwa::u32 m_WarnCount;
	wiwa::u32 m_ErrorCount;
};