#pragma once
#include <string>

#include <imgui.h>

#include <core/common.h>
#include <math/math.h>
#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>



enum class PanelCategory : wiwa::u8
{
	None = 0, // not listed in any menu: the editor opens it itself
	File = BIT(0),
	Window = BIT(1),
	Info = BIT(2),
	Renderer = BIT(3)
};

ENUM_CLASS_FLAG_OPERATORS(PanelCategory)

class Panel
{
public:
	Panel(const char* name, PanelCategory category)
		: m_Name(name), m_Category(category) {}

	virtual ~Panel() = default;

	virtual void on_update() {};
	virtual void on_draw() = 0;

	const char* get_name() const { return m_Name.c_str(); }
	// The name ImGui knows the window by (a panel whose title changes returns a stable "###id" here).
	virtual const char* window_name() const { return m_Name.c_str(); }
	void set_open(bool open) { m_Open = open; }
	PanelCategory get_category() const { return m_Category; }
	bool is_active() const { return m_Open; }
	void switch_active()
	{
		m_Open = !m_Open;
	}

	// How many times on_draw ran begin_window (counted even while the window is a hidden tab: a smoke check wants to know the panel was run).
	wiwa::u32 frames_drawn() const { return m_Frames; }
	// Brings the window to the front of its dock on the next frame (a smoke check wants the contents drawn).
	void request_focus() { m_Focus = true; }

protected:
	// The shared start of a panel's on_draw: counts the frame, honours request_focus, sets the first-use position and size and calls ImGui::Begin.
	// The caller calls ImGui::End either way.
	bool begin_window(ImVec2 pos, ImVec2 size)
	{
		m_Frames++;
		if (m_Focus)
		{
			ImGui::SetNextWindowFocus();
			m_Focus = false;
		}
		ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
		return ImGui::Begin(get_name(), &m_Open);
	}

	std::string m_Name;
	bool m_Open = true;
	PanelCategory m_Category;

private:
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;
};