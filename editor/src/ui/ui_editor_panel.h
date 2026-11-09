#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <imgui.h>

#include <panels/panel.h>
#include <scene/scene_editor.h>
#include <ui/ui_layout.h>

#include "ui_edit_model.h"

// The "UI Editor" panel: edits one .wuipanel. A widget tree on the left, the panel as the game draws it in the middle, the fields of the
// selected widget on the right.
//
// The middle is drawn by the renderer, like the Viewport: the panel builds the UiDrawList of the panel being edited with the same code the
// game uses and asks the RenderModule to draw it into a texture (RenderModule::set_ui_preview), which ImGui shows as an image. Clicking the
// image selects the widget under the mouse, dragging moves it, and the handles around the selection resize it (that writes anchors and
// offsets). A widget with a world anchor is shown at a stand-in position you can drag (kept only while the panel is open).
//
// Ctrl+S saves (the asset pipeline then cooks the file again), Ctrl+Z / Ctrl+Y undo and redo, Delete removes the selection.
// Main thread, between the ImGui frame's begin and end.
class UiEditorPanel final : public Panel
{
public:
	explicit UiEditorPanel(wiwa::SceneEditor& editor);
	virtual ~UiEditorPanel() override;

	// Loads the file into the panel and shows the panel. If the panel being edited has changes, it asks what to do with them first.
	void open(const std::filesystem::path& wuipanel);

	virtual void on_draw() override;
	virtual const char* window_name() const override { return "###UiEditor"; }

	// What the Maximize button and F11 do (the editor points them at its layout manager).
	void set_maximize(std::function<void()> toggle, std::function<bool()> isMaximized)
	{
		m_ToggleMaximize = std::move(toggle);
		m_IsMaximized = std::move(isMaximized);
	}

	// For the launch smoke: a file is loaded, and the canvas is being drawn by the renderer.
	bool loaded() const { return m_Model != nullptr && !m_Blocked; }
	bool previewing() const { return m_PreviewRunning; }

private:
	enum class DragMode
	{
		None,
		Move,
		Resize,
	};

	// The rect of the widget being dragged, as the drag changes it. Written to the document when the mouse is released.
	struct Drag
	{
		DragMode Mode = DragMode::None;
		wiwa::UiId Widget = 0;
		int Handle = -1;                 // Resize: 0 to 7 clockwise from the top left
		glm::vec2 StartPoint{ 0.0f };    // of the mouse on the canvas
		glm::vec2 StartOffsetMin{ 0.0f };
		glm::vec2 StartOffsetMax{ 0.0f };
		glm::vec2 StartStandIn{ 0.0f };
		glm::vec2 OffsetMin{ 0.0f };     // now
		glm::vec2 OffsetMax{ 0.0f };
		glm::vec2 StandIn{ 0.0f };
		bool Changed = false;
	};

	// A field being edited with a slider or a text box: shown at once, written to the document when the control is let go.
	struct PendingEdit
	{
		wiwa::UiId Widget = 0;
		std::string Field{};
		wiwa::FieldValue Value{};
	};

	enum class AskAction
	{
		None,
		Close,
		Open,
	};

	void draw_toolbar();
	void draw_tree();
	void draw_tree_node(wiwa::UiId parent);
	void draw_canvas();
	void draw_inspector();
	void draw_rect_section(const wiwa::UiWidget& widget);
	void draw_field(const wiwa::UiWidget& widget, const wiwa::UiField& field);
	void draw_unsaved_popup();
	bool tree_matches(wiwa::UiId id) const;

	bool save();
	void load(const std::filesystem::path& file);
	void select(wiwa::UiId id);
	void handle_shortcuts();
	void stop_preview();

	// The panel as the canvas shows it: pending edits, the drag in progress and the stand-ins for world anchors applied to a copy.
	wiwa::UiPanelData make_preview() const;
	glm::vec2 stand_in_of(wiwa::UiId id) const;
	glm::vec2 canvas_scale_to_reference() const;
	std::string describe_asset(const wiwa::AssetRef& ref) const;
	wiwa::UiId widget_at(const wiwa::UiPanelData& preview, const glm::vec2& point) const;
	bool in_stack(const wiwa::UiPanelData& data, const wiwa::UiWidget& widget) const;

	wiwa::SceneEditor& m_Editor;
	std::filesystem::path m_File{};
	Scope<wiwa::UiEditModel> m_Model{};
	std::string m_Error{};         // why the file could not be read or written
	bool m_Blocked = false;        // the file could not be read: nothing is written over it
	wiwa::UiId m_Selected = 0;
	std::optional<PendingEdit> m_Pending{};

	// canvas
	glm::vec2 m_Canvas{ 1920.0f, 1080.0f }; // pixels of the preview
	int m_Preset = 0;
	float m_Zoom = 0.5f;
	bool m_PreviewRunning = false;
	wiwa::UiLayoutResult m_Layout{};
	Drag m_Drag{};
	std::unordered_map<wiwa::UiId, glm::vec2> m_StandIns{}; // reference pixels, by widget id

	// asking what to do with unsaved changes
	AskAction m_Ask = AskAction::None;
	std::filesystem::path m_PendingOpen{};

	// the columns and the tree search
	float m_LeftWidth = 240.0f;
	float m_RightWidth = 330.0f;
	ImGuiTextFilter m_TreeFilter{};
	std::function<void()> m_ToggleMaximize{};
	std::function<bool()> m_IsMaximized{};

	std::string m_RenameBuffer{};
	wiwa::UiId m_Renaming = 0;
};
