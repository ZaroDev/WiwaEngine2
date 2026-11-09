#pragma once

#include <functional>
#include <string>
#include <vector>

// Ctrl+P: one search box over everything the editor can do or find: menu actions, panels, layouts, entities of the scene and assets.
// The items are collected by whoever opens it (the editor), so this class knows nothing of them. Main thread, between the ImGui frame's
// begin and end.
class CommandPalette
{
public:
	struct Item
	{
		std::string Label{};  // "Undo", "Cube", "Assets/models/shapes/shapes.glb"
		std::string Group{};  // "Action", "Panel", "Layout", "Entity", "Asset"
		std::string Detail{}; // the shortcut, or the folder
		std::function<void()> Run{};
	};

	// Shows the palette with these items (it replaces any it had).
	void open(std::vector<Item> items);
	bool is_open() const { return m_Open; }
	void draw();

	// 0 when the query is not a subsequence of the text (case-insensitive); higher is a better match. Empty query: 1.
	static int score(const std::string& query, const std::string& text);

private:
	std::vector<Item> m_Items{};
	bool m_Open = false;
	bool m_Appearing = false;
	char m_Query[96] = "";
	int m_Selected = 0;
};
