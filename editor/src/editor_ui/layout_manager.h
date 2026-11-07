#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include <panels/panel.h>

// Window layouts of the editor: the built-in presets (built with DockBuilder), layouts the user saved (ImGui ini text in a folder), and
// maximizing one panel over the dock area. ImGui's own imgui.ini keeps whatever layout was last on screen, so the editor opens as it was
// left. Main thread, between the ImGui frame's begin and end.
class LayoutManager
{
public:
	// folder holds the user layouts (one <name>.ini each). The first run (no imgui.ini yet) starts with the Default preset.
	void init(const std::filesystem::path& folder);

	static const std::vector<std::string>& builtin_names();
	std::vector<std::string> user_names() const;

	// Applied by apply_pending() on the next frame, before the dock space is drawn.
	void request_builtin(const std::string& name) { m_PendingBuiltin = name; }
	bool request_user(const std::string& name);
	// The live layout as <name>.ini in the folder. false if the name is empty or has path characters, or the file cannot be written.
	bool save_user(const std::string& name);
	bool delete_user(const std::string& name);
	static bool valid_name(const std::string& name);

	// Called inside dock_space() before ImGui::DockSpace(). A preset opens the panels it lists and closes the other panels of the menus.
	void apply_pending(ImGuiID dockspace, const std::vector<Ref<Panel>>& panels);

	// ---- maximize ----
	// Toggles: the panel fills the dock area over the others (which are not drawn) until it is toggled again, closed, or Esc is pressed.
	void toggle_maximize(Panel* panel);
	Panel* maximized() const { return m_Maximized; }
	// The panel under the mouse (or the focused one), for the shortcut.
	static Panel* panel_at_cursor(const std::vector<Ref<Panel>>& panels);
	// Every frame before the panels are drawn: keeps the maximized window over the dock area, restores it when it is closed or Esc is pressed.
	// allowEscape: Esc restores the panel (not while playing, when Esc belongs to the game, nor with the command palette open).
	void update_maximize(bool allowEscape);

private:
	// reapply: put the layout back as it was when the panel was maximized (at the start of the next frame)
	void restore_maximized(bool reapply);

	std::filesystem::path m_Folder{};
	std::string m_PendingBuiltin{};
	std::string m_PendingUserText{};
	bool m_HasPendingUser = false;

	Panel* m_Maximized = nullptr;
	std::string m_SavedLayout{}; // ImGui ini text taken when the panel was maximized
};
