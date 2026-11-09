#pragma once

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

// Every keyboard shortcut of the editor in one table, so that the menus, the F1 cheatsheet and the command palette show and run the same
// actions. Main thread, between the ImGui frame's begin and end.
struct ShortcutAction
{
	std::string Id{};    // "edit.undo"
	std::string Label{}; // "Undo"
	ImGuiKeyChord Chord = 0; // 0: menu and palette only
	std::function<void()> Run{};
	std::function<bool()> Enabled{}; // empty: always
};

class ShortcutRegistry
{
public:
	void add(ShortcutAction action);
	const std::vector<ShortcutAction>& all() const { return m_Actions; }
	const ShortcutAction* find(const std::string& id) const;

	bool enabled(const ShortcutAction& action) const { return !action.Enabled || action.Enabled(); }
	// Runs the action if it exists and is enabled.
	bool run(const std::string& id) const;
	// Fires the enabled action whose chord was pressed this frame. Nothing fires while a text field is being typed in.
	void poll() const;

	static std::string chord_text(ImGuiKeyChord chord);
	// The F1 window.
	void draw_cheatsheet(bool& open) const;

private:
	std::vector<ShortcutAction> m_Actions{};
};
