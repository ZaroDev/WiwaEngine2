#pragma once

#include <panels/panel.h>
#include <scene/scene_editor.h>

// The "History" panel: the undo steps of the scene, oldest first; the one the document is at is highlighted, and a click goes there.
class HistoryPanel : public Panel
{
public:
	explicit HistoryPanel(wiwa::SceneEditor& editor) : Panel("History", PanelCategory::Window), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;

private:
	wiwa::SceneEditor& m_Editor;
};
