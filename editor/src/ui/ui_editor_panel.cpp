#include <editor_common.h>
#include "ui_editor_panel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_module.h>
#include <ui/ui_panel_io.h>

#include "../panels/asset_field.h"

using namespace wiwa;

namespace
{
	struct Preset
	{
		const char* Label;
		glm::vec2 Size;
	};

	// The first one is the panel's own reference resolution.
	const Preset PRESETS[] = {
		{ "Reference resolution", { 0.0f, 0.0f } },
		{ "1920 x 1080", { 1920.0f, 1080.0f } },
		{ "1280 x 720", { 1280.0f, 720.0f } },
		{ "2560 x 1440", { 2560.0f, 1440.0f } },
		{ "3840 x 2160", { 3840.0f, 2160.0f } },
		{ "1080 x 1920 (portrait)", { 1080.0f, 1920.0f } },
	};

	constexpr WidgetKind KINDS[] = { WidgetKind::Panel, WidgetKind::HStack, WidgetKind::VStack, WidgetKind::Text, WidgetKind::Image,
		WidgetKind::Button, WidgetKind::Slider, WidgetKind::Toggle, WidgetKind::Bar, WidgetKind::ViewImage };

	constexpr const char* DRAG_PAYLOAD = "UI_WIDGET";
	constexpr float HANDLE_SIZE = 7.0f;

	bool is_container(WidgetKind kind)
	{
		return kind == WidgetKind::Panel || kind == WidgetKind::HStack || kind == WidgetKind::VStack || kind == WidgetKind::Button;
	}

	ImU32 color32(float r, float g, float b, float a = 1.0f)
	{
		return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
	}

	// The positions of the eight handles of a rect, clockwise from the top left.
	void handle_points(const UiRect& rect, glm::vec2 (&points)[8])
	{
		const glm::vec2 mid = (rect.Min + rect.Max) * 0.5f;
		points[0] = rect.Min;
		points[1] = { mid.x, rect.Min.y };
		points[2] = { rect.Max.x, rect.Min.y };
		points[3] = { rect.Max.x, mid.y };
		points[4] = rect.Max;
		points[5] = { mid.x, rect.Max.y };
		points[6] = { rect.Min.x, rect.Max.y };
		points[7] = { rect.Min.x, mid.y };
	}

	bool starts_with(const std::string& text, const char* prefix)
	{
		return text.rfind(prefix, 0) == 0;
	}
}

UiEditorPanel::UiEditorPanel(SceneEditor& editor) : Panel("UI Editor", PanelCategory::Window), m_Editor(editor)
{
	m_Open = false;
}

UiEditorPanel::~UiEditorPanel()
{
	stop_preview();
}

void UiEditorPanel::stop_preview()
{
	if (!m_PreviewRunning)
	{
		return;
	}
	m_PreviewRunning = false;
	if (RenderModule* render = Engine::get().get_module<RenderModule>())
	{
		render->clear_ui_preview();
	}
}

// ---- the document ----------------------------------------------------------------------------------------------------------------

void UiEditorPanel::open(const std::filesystem::path& wuipanel)
{
	if (m_Model != nullptr && wuipanel == m_File)
	{
		m_Open = true;
		return;
	}
	if (m_Model != nullptr && m_Model->dirty())
	{
		m_PendingOpen = wuipanel;
		m_Ask = AskAction::Open;
		m_Open = true;
		return;
	}
	load(wuipanel);
	m_Open = true;
}

void UiEditorPanel::load(const std::filesystem::path& file)
{
	m_File = file;
	m_Error.clear();
	m_Blocked = false;
	m_Selected = 0;
	m_Pending.reset();
	m_Drag = Drag{};
	m_StandIns.clear();
	m_Preset = 0;

	UiPanelData data;
	std::ifstream in(file, std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (!in && !text.empty())
	{
		m_Error = "Cannot read " + file.string();
		m_Blocked = true;
	}
	else
	{
		std::string error;
		if (std::optional<UiPanelData> parsed = ui_panel_from_text(text, error))
		{
			data = std::move(*parsed);
		}
		else
		{
			m_Error = file.filename().string() + ": " + error;
			m_Blocked = true; // the file is not overwritten with an empty panel
		}
	}
	m_Model = CreateScope<UiEditModel>(std::move(data));
	m_Canvas = m_Model->data().ReferenceResolution;
}

bool UiEditorPanel::save()
{
	if (m_Model == nullptr || m_Blocked || m_File.empty())
	{
		return false;
	}

	std::ofstream out(m_File, std::ios::binary | std::ios::trunc);
	out << ui_panel_to_json(m_Model->data()).dump(2);
	out.close();
	if (!out)
	{
		m_Error = "Cannot write " + m_File.string();
		return false;
	}
	m_Error.clear();
	m_Model->mark_saved();
	return true;
}

void UiEditorPanel::select(UiId id)
{
	if (id != m_Selected)
	{
		m_Pending.reset();
	}
	m_Selected = id;
}

void UiEditorPanel::handle_shortcuts()
{
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || m_Model == nullptr)
	{
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		save();
	}
	if (!io.WantTextInput)
	{
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true) && !io.KeyShift)
		{
			m_Pending.reset();
			m_Model->undo();
		}
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, true) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true))))
		{
			m_Pending.reset();
			m_Model->redo();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_Selected != 0)
		{
			m_Model->remove(m_Selected);
			m_Selected = 0;
		}
	}
}

// ---- what the canvas draws -------------------------------------------------------------------------------------------------------

glm::vec2 UiEditorPanel::stand_in_of(UiId id) const
{
	if (m_Drag.Mode == DragMode::Move && m_Drag.Widget == id)
	{
		return m_Drag.StandIn;
	}
	const auto found = m_StandIns.find(id);
	if (found != m_StandIns.end())
	{
		return found->second;
	}

	// the middle of the panel, and a little further for each widget that has none yet
	const UiPanelData& data = m_Model->data();
	size_t index = 0;
	for (const UiWidget& widget : data.Widgets)
	{
		if (widget.Id == id)
		{
			break;
		}
		index += widget.world_anchored() ? 1 : 0;
	}
	return data.ReferenceResolution * 0.5f + glm::vec2(48.0f, 48.0f) * static_cast<float>(index);
}

UiPanelData UiEditorPanel::make_preview() const
{
	UiPanelData data = m_Model->data();

	if (m_Pending)
	{
		if (UiWidget* widget = data.find(m_Pending->Widget))
		{
			if (const UiField* field = find_ui_field(m_Pending->Field))
			{
				field->Set(*widget, m_Pending->Value);
			}
		}
	}

	if (m_Drag.Mode != DragMode::None && m_Drag.Changed)
	{
		if (UiWidget* widget = data.find(m_Drag.Widget); widget != nullptr && !(m_Drag.Mode == DragMode::Move && widget->world_anchored()))
		{
			widget->OffsetMin = m_Drag.OffsetMin;
			widget->OffsetMax = m_Drag.OffsetMax;
		}
	}

	// A world anchored widget has no place in the editor: it is shown at a stand-in position on the canvas, as if the entity were there.
	for (UiWidget& widget : data.Widgets)
	{
		if (!widget.world_anchored())
		{
			continue;
		}
		const glm::vec2 size = glm::max(widget.OffsetMax - widget.OffsetMin, glm::vec2(0.0f));
		const glm::vec2 point = stand_in_of(widget.Id);
		widget.WorldEntity = EntityGuid{};
		widget.Parent = 0;
		widget.AnchorMin = widget.AnchorMax = glm::vec2(0.0f);
		widget.OffsetMin = point - glm::clamp(widget.Pivot, glm::vec2(0.0f), glm::vec2(1.0f)) * size;
		widget.OffsetMax = widget.OffsetMin + size;
	}
	return data;
}

UiId UiEditorPanel::widget_at(const UiPanelData& preview, const glm::vec2& point) const
{
	const std::vector<UiDrawItem> order = ui_draw_order(preview, m_Layout, m_Canvas);
	for (auto it = order.rbegin(); it != order.rend(); ++it)
	{
		const bool inClip = point.x >= it->Clip.x && point.y >= it->Clip.y && point.x < it->Clip.z && point.y < it->Clip.w;
		if (inClip && m_Layout.Rects[it->Index].contains(point))
		{
			return preview.Widgets[it->Index].Id;
		}
	}
	return 0;
}

bool UiEditorPanel::in_stack(const UiPanelData& data, const UiWidget& widget) const
{
	if (widget.world_anchored() || widget.Parent == 0)
	{
		return false;
	}
	const UiWidget* parent = data.find(widget.Parent);
	return parent != nullptr && (parent->Kind == WidgetKind::HStack || parent->Kind == WidgetKind::VStack);
}

std::string UiEditorPanel::describe_asset(const AssetRef& ref) const
{
	if (!ref.valid())
	{
		return "(none)";
	}
	if (AssetManager* assets = Engine::get().get_module<AssetManager>())
	{
		for (const AssetCatalog::Entry& entry : assets->catalog_entries())
		{
			if (entry.Guid == ref.Guid)
			{
				return std::filesystem::path(entry.Path).filename().string();
			}
		}
	}
	return ref.Guid.to_string().substr(0, 8);
}

// ---- drawing the panel -----------------------------------------------------------------------------------------------------------

void UiEditorPanel::on_draw()
{
	if (!m_Open)
	{
		stop_preview();
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(260.0f, 90.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);

	std::string title = "UI Editor";
	if (m_Model != nullptr)
	{
		title += " - " + m_File.filename().string() + (m_Model->dirty() ? "*" : "");
	}
	title += "###UiEditor";

	bool open = true;
	const bool visible = ImGui::Begin(title.c_str(), &open);
	if (!open)
	{
		if (m_Model != nullptr && m_Model->dirty())
		{
			m_Ask = AskAction::Close; // not closed until it is said what happens to the changes
		}
		else
		{
			m_Open = false;
			m_Model.reset();
			m_File.clear();
		}
	}

	if (visible && m_Open)
	{
		if (m_Model == nullptr)
		{
			ImGui::TextDisabled("Double-click a .wuipanel in the Asset Browser to edit it.");
		}
		else
		{
			if (!m_Error.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				ImGui::TextWrapped("%s", m_Error.c_str());
				ImGui::PopStyleColor();
			}
			if (m_Selected != 0 && m_Model->data().find(m_Selected) == nullptr)
			{
				m_Selected = 0;
			}

			handle_shortcuts();
			draw_toolbar();
			ImGui::Separator();

			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float splitter = 6.0f;
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float widest = std::max(available.x * 0.4f, 200.0f);
			m_LeftWidth = std::clamp(m_LeftWidth, 160.0f, widest);
			m_RightWidth = std::clamp(m_RightWidth, 200.0f, widest);
			const float leftWidth = m_LeftWidth;
			const float rightWidth = m_RightWidth;
			const float middleWidth = std::max(available.x - leftWidth - rightWidth - splitter * 2.0f - spacing * 4.0f, 120.0f);

			ImGui::BeginChild("##ui_tree", ImVec2(leftWidth, available.y), ImGuiChildFlags_Borders);
			draw_tree();
			ImGui::EndChild();
			ImGui::SameLine();

			ImGui::InvisibleButton("##split_left", ImVec2(splitter, available.y));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			if (ImGui::IsItemActive())
			{
				m_LeftWidth += ImGui::GetIO().MouseDelta.x;
			}
			ImGui::SameLine();

			ImGui::BeginChild("##ui_canvas_area", ImVec2(middleWidth, available.y), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			draw_canvas();
			ImGui::EndChild();
			ImGui::SameLine();

			ImGui::InvisibleButton("##split_right", ImVec2(splitter, available.y));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			if (ImGui::IsItemActive())
			{
				m_RightWidth -= ImGui::GetIO().MouseDelta.x;
			}
			ImGui::SameLine();

			ImGui::BeginChild("##ui_inspector", ImVec2(rightWidth, available.y), ImGuiChildFlags_Borders);
			draw_inspector();
			ImGui::EndChild();
		}
		draw_unsaved_popup();
	}
	ImGui::End();

	if (!visible || !m_Open || m_Model == nullptr)
	{
		stop_preview();
	}
}

void UiEditorPanel::draw_unsaved_popup()
{
	if (m_Ask == AskAction::None)
	{
		return;
	}
	if (!ImGui::IsPopupOpen("Unsaved changes"))
	{
		ImGui::OpenPopup("Unsaved changes");
	}

	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("%s has changes that are not saved.", m_File.filename().string().c_str());

		bool proceed = false;
		bool saved = true;
		if (ImGui::Button("Save"))
		{
			saved = save();
			proceed = saved;
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't save"))
		{
			proceed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_Ask = AskAction::None;
			ImGui::CloseCurrentPopup();
		}

		if (proceed && saved)
		{
			const AskAction action = m_Ask;
			m_Ask = AskAction::None;
			ImGui::CloseCurrentPopup();
			if (action == AskAction::Close)
			{
				m_Open = false;
				m_Model.reset(); // what was not saved is gone: the next open reads the file
				m_File.clear();
			}
			else if (action == AskAction::Open)
			{
				load(m_PendingOpen);
			}
		}
		ImGui::EndPopup();
	}
}

void UiEditorPanel::draw_toolbar()
{
	ImGui::BeginDisabled(m_Blocked || !m_Model->dirty());
	if (ImGui::Button("Save"))
	{
		save();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(m_Blocked || !m_Model->dirty());
	if (ImGui::Button("Revert"))
	{
		m_PendingOpen = m_File; // asks first: reloading the file drops the changes
		m_Ask = AskAction::Open;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Reload the file and drop the unsaved changes");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Model->can_undo());
	if (ImGui::Button("Undo"))
	{
		m_Pending.reset();
		m_Model->undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Model->can_redo());
	if (ImGui::Button("Redo"))
	{
		m_Pending.reset();
		m_Model->redo();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(190.0f);
	if (ImGui::BeginCombo("##preset", PRESETS[m_Preset].Label))
	{
		for (int i = 0; i < static_cast<int>(std::size(PRESETS)); i++)
		{
			if (ImGui::Selectable(PRESETS[i].Label, i == m_Preset))
			{
				m_Preset = i;
			}
		}
		ImGui::EndCombo();
	}
	m_Canvas = m_Preset == 0 ? m_Model->data().ReferenceResolution : PRESETS[m_Preset].Size;
	m_Canvas = glm::clamp(glm::floor(m_Canvas), glm::vec2(1.0f), glm::vec2(8192.0f));

	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("##zoom", &m_Zoom, 0.1f, 4.0f, "zoom %.2f");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
	{
		ImGui::SetTooltip("Mouse wheel over the canvas zooms, the middle button pans");
	}
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
	{
		// the middle column is about what is left of the window
		const float room = std::max(ImGui::GetWindowWidth() - m_LeftWidth - m_RightWidth - 60.0f, 120.0f);
		m_Zoom = std::clamp(std::min(room / m_Canvas.x, (ImGui::GetWindowHeight() - 120.0f) / m_Canvas.y), 0.1f, 4.0f);
	}
	if (m_ToggleMaximize)
	{
		ImGui::SameLine();
		const bool maximized = m_IsMaximized && m_IsMaximized();
		if (ImGui::Button(maximized ? ICON_FK_COMPRESS " Restore" : ICON_FK_EXPAND " Maximize"))
		{
			m_ToggleMaximize();
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip("F11 or Shift+Space; Esc restores");
		}
	}
}

void UiEditorPanel::draw_tree()
{
	if (ImGui::Button("Add widget"))
	{
		ImGui::OpenPopup("##add_widget");
	}
	if (ImGui::BeginPopup("##add_widget"))
	{
		const UiWidget* selected = m_Model->data().find(m_Selected);
		const UiId parent = selected == nullptr ? 0 : (is_container(selected->Kind) ? selected->Id : selected->Parent);
		const UiWidget* parentWidget = m_Model->data().find(parent);
		if (parentWidget)
		{
			ImGui::TextDisabled("inside %s", parentWidget->Name.c_str());
		}
		else
		{
			ImGui::TextDisabled("at the top level");
		}
		
		for (const WidgetKind kind : KINDS)
		{
			if (ImGui::MenuItem(widget_kind_name(kind)))
			{
				if (const UiId added = m_Model->add(kind, parent))
				{
					select(added);
				}
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(m_Selected == 0);
	if (ImGui::Button("Delete"))
	{
		m_Model->remove(m_Selected);
		m_Selected = 0;
	}
	ImGui::EndDisabled();
	m_TreeFilter.Draw("##tree_filter", -1.0f);
	ImGui::Separator();

	draw_tree_node(0);

	// dropping on the empty space under the tree puts a widget at the top level
	const ImVec2 rest = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("##tree_rest", ImVec2(std::max(rest.x, 1.0f), std::max(rest.y, 24.0f)));
	if (ImGui::IsItemClicked())
	{
		select(0);
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD))
		{
			m_Model->reparent(*static_cast<const UiId*>(payload->Data), 0, std::numeric_limits<size_t>::max());
		}
		ImGui::EndDragDropTarget();
	}
}

// Whether the widget or something below it passes the search box.
bool UiEditorPanel::tree_matches(UiId id) const
{
	const UiWidget* widget = m_Model->data().find(id);
	if (widget == nullptr)
	{
		return false;
	}
	if (m_TreeFilter.PassFilter(widget->Name.c_str()) || m_TreeFilter.PassFilter(widget_kind_name(widget->Kind)))
	{
		return true;
	}
	for (const UiWidget& other : m_Model->data().Widgets)
	{
		if (other.Parent == id && tree_matches(other.Id))
		{
			return true;
		}
	}
	return false;
}

void UiEditorPanel::draw_tree_node(UiId parent)
{
	// the ids first: an action in the menu changes the panel under the loop
	std::vector<UiId> children;
	for (const UiWidget& widget : m_Model->data().Widgets)
	{
		if (widget.Parent == parent)
		{
			children.push_back(widget.Id);
		}
	}

	for (size_t position = 0; position < children.size(); position++)
	{
		const UiId id = children[position];
		const UiWidget* widget = m_Model->data().find(id);
		if (widget == nullptr || (m_TreeFilter.IsActive() && !tree_matches(id)))
		{
			continue;
		}
		if (m_TreeFilter.IsActive())
		{
			ImGui::SetNextItemOpen(true); // show what matched
		}

		const bool hasChildren = std::any_of(m_Model->data().Widgets.begin(), m_Model->data().Widgets.end(), [id](const UiWidget& other) { return other.Parent == id; });
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
		flags |= hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf;
		flags |= id == m_Selected ? ImGuiTreeNodeFlags_Selected : 0;

		ImGui::PushID(static_cast<int>(id));
		const std::string label = widget->Name + "  (" + widget_kind_name(widget->Kind) + ")" + (widget->world_anchored() ? "  [world]" : "");
		const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			select(id);
		}

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(DRAG_PAYLOAD, &id, sizeof(id));
			ImGui::TextUnformatted(widget->Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD))
			{
				m_Model->reparent(*static_cast<const UiId*>(payload->Data), id, std::numeric_limits<size_t>::max());
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##node_menu"))
		{
			select(id);
			if (ImGui::BeginMenu("Add child"))
			{
				for (const WidgetKind kind : KINDS)
				{
					if (ImGui::MenuItem(widget_kind_name(kind)))
					{
						if (const UiId added = m_Model->add(kind, id))
						{
							select(added);
						}
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Duplicate"))
			{
				if (const UiId copy = m_Model->duplicate(id))
				{
					select(copy);
				}
			}
			if (ImGui::MenuItem("Move up", nullptr, false, position > 0))
			{
				m_Model->reparent(id, parent, position - 1);
			}
			if (ImGui::MenuItem("Move down", nullptr, false, position + 1 < children.size()))
			{
				m_Model->reparent(id, parent, position + 1);
			}
			if (ImGui::MenuItem("Rename"))
			{
				m_Renaming = id;
				m_RenameBuffer = widget->Name;
			}
			if (ImGui::MenuItem("Delete"))
			{
				m_Model->remove(id);
				m_Selected = m_Selected == id ? 0 : m_Selected;
			}
			ImGui::EndPopup();
		}

		if (m_Renaming == id)
		{
			char buffer[128];
			std::snprintf(buffer, sizeof(buffer), "%s", m_RenameBuffer.c_str());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##rename", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				if (!m_Model->rename(id, buffer))
				{
					m_Error = "That name is empty, has a '/', or is taken by a sibling.";
				}
				else
				{
					m_Error.clear();
				}
				m_Renaming = 0;
			}
			else if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_Renaming = 0;
			}
			m_RenameBuffer = buffer;
		}

		if (opened)
		{
			if (hasChildren)
			{
				draw_tree_node(id);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void UiEditorPanel::draw_canvas()
{
	RenderModule* render = Engine::get().get_module<RenderModule>();
	UiModule* ui = Engine::get().get_module<UiModule>();
	if (render == nullptr || !render->is_ready())
	{
		ImGui::TextDisabled("The renderer is not running.");
		return;
	}

	// wheel zoom and middle-button pan while the mouse is over the canvas area
	if (ImGui::IsWindowHovered())
	{
		const ImGuiIO& canvasIo = ImGui::GetIO();
		if (canvasIo.MouseWheel != 0.0f)
		{
			m_Zoom = std::clamp(m_Zoom * std::pow(1.1f, canvasIo.MouseWheel), 0.1f, 4.0f);
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
		{
			ImGui::SetScrollX(ImGui::GetScrollX() - canvasIo.MouseDelta.x);
			ImGui::SetScrollY(ImGui::GetScrollY() - canvasIo.MouseDelta.y);
		}
	}

	// what the game draws for this panel, drawn by the renderer into a texture (like the Viewport), and shown here
	const UiPanelData preview = make_preview();
	UiLayoutInput input;
	input.CanvasSize = m_Canvas;
	input.Fonts = [ui](const AssetRef& ref) -> const FontData* { return ui != nullptr ? ui->font_for(ref) : nullptr; };
	input.TextureSize = [ui](const AssetRef& ref) -> std::optional<glm::vec2> { return ui != nullptr ? ui->texture_size_for(ref) : std::nullopt; };
	m_Layout = compute_layout(preview, input);

	const std::vector<UiInteractionState> states(preview.Widgets.size(), UiInteractionState::Normal);
	UiDrawList list;
	append_panel(list, preview, states, m_Layout, input);
	render->set_ui_preview(std::move(list), static_cast<u32>(m_Canvas.x), static_cast<u32>(m_Canvas.y), glm::vec4(0.11f, 0.11f, 0.13f, 1.0f));
	m_PreviewRunning = true;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::Image(static_cast<ImTextureID>(render->get_ui_preview_texture_id()), ImVec2(m_Canvas.x * m_Zoom, m_Canvas.y * m_Zoom));
	const bool hovered = ImGui::IsItemHovered();

	const ImGuiIO& io = ImGui::GetIO();
	const glm::vec2 point = (glm::vec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y)) / m_Zoom;
	const auto to_screen = [&](const glm::vec2& canvas) { return ImVec2(origin.x + canvas.x * m_Zoom, origin.y + canvas.y * m_Zoom); };

	const auto index_of = [&](UiId id) -> int
	{
		for (size_t i = 0; i < preview.Widgets.size(); i++)
		{
			if (preview.Widgets[i].Id == id)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	};

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const int selectedIndex = index_of(m_Selected);

	// the handles of the selection, and which one the mouse is on
	int handleUnderMouse = -1;
	glm::vec2 handles[8];
	if (selectedIndex >= 0 && m_Layout.Visible[selectedIndex])
	{
		handle_points(m_Layout.Rects[selectedIndex], handles);
		for (int i = 0; i < 8; i++)
		{
			const ImVec2 screen = to_screen(handles[i]);
			if (std::abs(io.MousePos.x - screen.x) <= HANDLE_SIZE && std::abs(io.MousePos.y - screen.y) <= HANDLE_SIZE)
			{
				handleUnderMouse = i;
			}
		}
	}

	// press: a handle of the selection, else the widget under the mouse
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_Drag.Mode == DragMode::None)
	{
		UiId target = m_Selected;
		DragMode mode = DragMode::None;
		if (handleUnderMouse >= 0)
		{
			mode = DragMode::Resize;
		}
		else
		{
			target = widget_at(preview, point);
			select(target);
			mode = target != 0 ? DragMode::Move : DragMode::None;
		}

		if (mode != DragMode::None)
		{
			if (const UiWidget* widget = m_Model->data().find(target))
			{
				m_Drag = Drag{};
				m_Drag.Mode = mode;
				m_Drag.Widget = target;
				m_Drag.Handle = handleUnderMouse;
				m_Drag.StartPoint = point;
				m_Drag.StartOffsetMin = m_Drag.OffsetMin = widget->OffsetMin;
				m_Drag.StartOffsetMax = m_Drag.OffsetMax = widget->OffsetMax;
				m_Drag.StartStandIn = m_Drag.StandIn = stand_in_of(target);
			}
		}
	}

	// moving: the document changes when the mouse is let go, so that a drag is one step of the undo history
	if (m_Drag.Mode != DragMode::None)
	{
		const UiWidget* widget = m_Model->data().find(m_Drag.Widget);
		const int dragged = index_of(m_Drag.Widget);
		if (widget == nullptr || dragged < 0)
		{
			m_Drag = Drag{};
		}
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const float scale = m_Layout.Scale[dragged] > 0.0f ? m_Layout.Scale[dragged] : 1.0f;
			glm::vec2 delta = (point - m_Drag.StartPoint) / scale;
			if (io.KeyCtrl)
			{
				delta = glm::round(delta / 8.0f) * 8.0f; // Ctrl snaps to a grid of 8 reference pixels
			}

			if (m_Drag.Mode == DragMode::Move)
			{
				if (widget->world_anchored())
				{
					m_Drag.StandIn = m_Drag.StartStandIn + delta;
					m_Drag.Changed = delta != glm::vec2(0.0f);
				}
				else if (!in_stack(m_Model->data(), *widget))
				{
					m_Drag.OffsetMin = m_Drag.StartOffsetMin + delta;
					m_Drag.OffsetMax = m_Drag.StartOffsetMax + delta;
					m_Drag.Changed = delta != glm::vec2(0.0f);
				}
			}
			else
			{
				const int h = m_Drag.Handle;
				const bool left = h == 0 || h == 6 || h == 7;
				const bool right = h == 2 || h == 3 || h == 4;
				const bool top = h == 0 || h == 1 || h == 2;
				const bool bottom = h == 4 || h == 5 || h == 6;
				m_Drag.OffsetMin = m_Drag.StartOffsetMin;
				m_Drag.OffsetMax = m_Drag.StartOffsetMax;
				m_Drag.OffsetMin.x += left ? delta.x : 0.0f;
				m_Drag.OffsetMax.x += right ? delta.x : 0.0f;
				m_Drag.OffsetMin.y += top ? delta.y : 0.0f;
				m_Drag.OffsetMax.y += bottom ? delta.y : 0.0f;
				m_Drag.Changed = m_Drag.OffsetMin != m_Drag.StartOffsetMin || m_Drag.OffsetMax != m_Drag.StartOffsetMax;
			}
		}
		else
		{
			if (m_Drag.Changed)
			{
				if (m_Drag.Mode == DragMode::Move && widget->world_anchored())
				{
					m_StandIns[m_Drag.Widget] = m_Drag.StandIn;
				}
				else
				{
					m_Model->set_rect(m_Drag.Widget, widget->AnchorMin, widget->AnchorMax, m_Drag.OffsetMin, m_Drag.OffsetMax);
				}
			}
			m_Drag = Drag{};
		}
	}

	// overlays: the widget under the mouse, the selection and its handles
	if (hovered && m_Drag.Mode == DragMode::None && handleUnderMouse < 0)
	{
		const UiId under = widget_at(preview, point);
		const int index = index_of(under);
		if (index >= 0 && under != m_Selected)
		{
			draw->AddRect(to_screen(m_Layout.Rects[index].Min), to_screen(m_Layout.Rects[index].Max), color32(1.0f, 1.0f, 1.0f, 0.35f));
		}
	}
	if (selectedIndex >= 0 && m_Layout.Visible[selectedIndex])
	{
		const UiRect& rect = m_Layout.Rects[selectedIndex];
		draw->AddRect(to_screen(rect.Min), to_screen(rect.Max), color32(0.2f, 0.7f, 1.0f), 0.0f, 0, 2.0f);
		handle_points(rect, handles);
		for (int i = 0; i < 8; i++)
		{
			const ImVec2 screen = to_screen(handles[i]);
			draw->AddRectFilled(ImVec2(screen.x - 4.0f, screen.y - 4.0f), ImVec2(screen.x + 4.0f, screen.y + 4.0f), i == handleUnderMouse ? color32(1.0f, 1.0f, 0.3f) : color32(0.2f, 0.7f, 1.0f));
		}
		if (hovered && in_stack(m_Model->data(), *m_Model->data().find(m_Selected)) && m_Drag.Mode == DragMode::Move)
		{
			ImGui::SetTooltip("In a stack: the stack places it. Reorder it in the tree, resize it with the handles.");
		}
	}
	if (selectedIndex >= 0 && !m_Layout.Visible[selectedIndex])
	{
		draw->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), color32(1.0f, 0.8f, 0.3f), "The selection is hidden");
	}
}

// ---- the fields ------------------------------------------------------------------------------------------------------------------

void UiEditorPanel::draw_inspector()
{
	if (m_Selected == 0)
	{
		ImGui::TextUnformatted("Panel");
		ImGui::Separator();
		const UiPanelData& data = m_Model->data();
		glm::vec2 resolution = data.ReferenceResolution;
		float match = data.MatchWidthOrHeight;
		bool changed = ImGui::DragFloat2("Reference resolution", &resolution.x, 1.0f, 16.0f, 8192.0f, "%.0f");
		const bool finished1 = ImGui::IsItemDeactivatedAfterEdit();
		changed |= ImGui::SliderFloat("Match width / height", &match, 0.0f, 1.0f);
		const bool finished2 = ImGui::IsItemDeactivatedAfterEdit();
		if (changed && (finished1 || finished2))
		{
			m_Model->set_reference(resolution, match);
		}
		ImGui::TextDisabled("0 scales with the width of the screen, 1 with its height.");
		ImGui::Spacing();
		ImGui::TextDisabled("Click a widget in the tree or on the canvas to edit it.");
		return;
	}

	const UiWidget* widget = m_Model->data().find(m_Selected);
	if (widget == nullptr)
	{
		return;
	}

	ImGui::Text("%s  (%s)", widget->Name.c_str(), widget_kind_name(widget->Kind));
	ImGui::Separator();
	draw_rect_section(*widget);

	bool worldOpen = false;
	bool worldHeaderDone = false;
	const UiWidget current = *widget; // the fields draw from a copy: an edit may change the panel under them
	for (const UiField& field : ui_fields())
	{
		if (!ui_field_visible(current.Kind, field.Name))
		{
			continue;
		}
		if (starts_with(field.Name, "AnchorM") || starts_with(field.Name, "OffsetM") || field.Name == "Pivot")
		{
			continue; // in the rect section
		}

		if (starts_with(field.Name, "World"))
		{
			if (!worldHeaderDone)
			{
				worldHeaderDone = true;
				worldOpen = ImGui::CollapsingHeader("World anchor", current.world_anchored() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
			}
			if (!worldOpen)
			{
				continue;
			}
		}
		draw_field(current, field);
	}
}

void UiEditorPanel::draw_rect_section(const UiWidget& widget)
{
	if (!ImGui::CollapsingHeader("Rect", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	// anchor presets: the widget keeps its size and goes to the anchor
	if (ImGui::Button("Anchor preset..."))
	{
		ImGui::OpenPopup("##anchor_presets");
	}
	if (ImGui::BeginPopup("##anchor_presets"))
	{
		const glm::vec2 size = glm::max(widget.OffsetMax - widget.OffsetMin, glm::vec2(1.0f));
		const char* names[3][3] = { { "TL", "T", "TR" }, { "L", "C", "R" }, { "BL", "B", "BR" } };
		for (int row = 0; row < 3; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				if (column > 0)
				{
					ImGui::SameLine();
				}
				if (ImGui::Button(names[row][column], ImVec2(40.0f, 28.0f)))
				{
					const glm::vec2 anchor(static_cast<float>(column) * 0.5f, static_cast<float>(row) * 0.5f);
					const glm::vec2 offsetMin = -anchor * size;
					m_Model->set_rect(widget.Id, anchor, anchor, offsetMin, offsetMin + size);
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::Separator();
		if (ImGui::Button("Stretch both", ImVec2(130.0f, 0.0f)))
		{
			m_Model->set_rect(widget.Id, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f });
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button("Stretch width", ImVec2(130.0f, 0.0f)))
		{
			m_Model->set_rect(widget.Id, { 0.0f, widget.AnchorMin.y }, { 1.0f, widget.AnchorMin.y }, { 0.0f, widget.OffsetMin.y }, { 0.0f, widget.OffsetMax.y });
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button("Stretch height", ImVec2(130.0f, 0.0f)))
		{
			m_Model->set_rect(widget.Id, { widget.AnchorMin.x, 0.0f }, { widget.AnchorMin.x, 1.0f }, { widget.OffsetMin.x, 0.0f }, { widget.OffsetMax.x, 0.0f });
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (widget.Parent != 0)
	{
		const UiWidget* parent = m_Model->data().find(widget.Parent);
		if (parent != nullptr && (parent->Kind == WidgetKind::HStack || parent->Kind == WidgetKind::VStack) && !widget.world_anchored())
		{
			ImGui::TextDisabled("In a stack: only the size (offset max - min) counts.");
		}
	}

	for (const char* name : { "AnchorMin", "AnchorMax", "Pivot", "OffsetMin", "OffsetMax" })
	{
		if (const UiField* field = find_ui_field(name))
		{
			draw_field(widget, *field);
		}
	}
}

void UiEditorPanel::draw_field(const UiWidget& widget, const UiField& field)
{
	const bool pendingHere = m_Pending && m_Pending->Widget == widget.Id && m_Pending->Field == field.Name;
	FieldValue edited = pendingHere ? m_Pending->Value : field.Get(widget);

	bool changed = false;
	bool commitNow = false;
	bool finished = false;
	const char* label = field.Name.c_str();
	ImGui::PushID(label);
	ImGui::SetNextItemWidth(170.0f);

	switch (field.Type)
	{
	case FieldType::Bool:
	{
		bool value = std::get<bool>(edited);
		if (ImGui::Checkbox(label, &value))
		{
			edited = value;
			changed = commitNow = true;
		}
		break;
	}
	case FieldType::Int:
	{
		i32 value = std::get<i32>(edited);
		changed = ImGui::DragInt(label, &value);
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::Float:
	{
		float value = std::get<float>(edited);
		if (field.Meta.HasRange)
		{
			changed = ImGui::SliderFloat(label, &value, field.Meta.Min, field.Meta.Max);
		}
		else
		{
			changed = ImGui::DragFloat(label, &value, 0.5f);
		}
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::Vec2:
	{
		glm::vec2 value = std::get<glm::vec2>(edited);
		changed = ImGui::DragFloat2(label, &value.x, 0.5f);
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::Vec3:
	{
		glm::vec3 value = std::get<glm::vec3>(edited);
		changed = ImGui::DragFloat3(label, &value.x, 0.1f);
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::Vec4:
	{
		glm::vec4 value = std::get<glm::vec4>(edited);
		changed = ImGui::DragFloat4(label, &value.x, 0.5f);
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::Color:
	{
		glm::vec4 value = std::get<glm::vec4>(edited);
		changed = ImGui::ColorEdit4(label, &value.x);
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = value;
		break;
	}
	case FieldType::String:
	{
		char buffer[1024];
		std::snprintf(buffer, sizeof(buffer), "%s", std::get<std::string>(edited).c_str());
		if (field.Name == "Text")
		{
			ImGui::SetNextItemWidth(-1.0f);
			changed = ImGui::InputTextMultiline("##text", buffer, sizeof(buffer), ImVec2(-1.0f, 60.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted("");
		}
		else
		{
			changed = ImGui::InputText(label, buffer, sizeof(buffer));
		}
		finished = ImGui::IsItemDeactivatedAfterEdit();
		edited = std::string(buffer);
		break;
	}
	case FieldType::Enum:
	{
		i32 value = std::get<i32>(edited);
		const std::vector<std::string>& names = field.Meta.EnumNames;
		if (ImGui::BeginCombo(label, value >= 0 && static_cast<size_t>(value) < names.size() ? names[static_cast<size_t>(value)].c_str() : "?"))
		{
			for (size_t i = 0; i < names.size(); i++)
			{
				if (ImGui::Selectable(names[i].c_str(), static_cast<i32>(i) == value))
				{
					edited = static_cast<i32>(i);
					changed = commitNow = true;
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	case FieldType::Asset:
	{
		const AssetRef& ref = std::get<AssetRef>(edited);
		std::string dropped;
		switch (draw_asset_field(label, describe_asset(ref), ref.valid(), dropped))
		{
		case AssetFieldEvent::Dropped:
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (const std::optional<AssetRef> found = assets != nullptr ? assets->find_asset(dropped) : std::nullopt)
			{
				edited = *found;
				changed = commitNow = true;
			}
			break;
		}
		case AssetFieldEvent::Cleared:
			edited = AssetRef{};
			changed = commitNow = true;
			break;
		default:
			break;
		}
		break;
	}
	case FieldType::Entity:
	{
		const EntityGuid guid = std::get<EntityGuid>(edited);
		Ref<Scene> scene = m_Editor.get_scene();
		std::string preview = "(none)";
		if (guid.valid())
		{
			const entt::entity entity = scene != nullptr ? scene->find(guid) : entt::entity{ entt::null };
			preview = entity != entt::null && scene->registry().all_of<Name>(entity) ? scene->registry().get<Name>(entity).Value : "(not in this scene) " + guid.to_string().substr(0, 8);
		}
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			if (ImGui::Selectable("(none)", !guid.valid()))
			{
				edited = EntityGuid{};
				changed = commitNow = true;
			}
			if (scene != nullptr)
			{
				for (const auto [entity, id, name] : scene->registry().view<Id, Name>().each())
				{
					ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
					if (ImGui::Selectable(name.Value.c_str(), id.Guid == guid))
					{
						edited = id.Guid;
						changed = commitNow = true;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	default:
		ImGui::TextDisabled("%s", label);
		break;
	}
	ImGui::PopID();

	if (changed)
	{
		m_Pending = PendingEdit{ widget.Id, field.Name, edited };
		if (commitNow)
		{
			m_Model->set_field(widget.Id, field.Name, edited);
			m_Pending.reset();
		}
	}
	if (finished && m_Pending && m_Pending->Widget == widget.Id && m_Pending->Field == field.Name)
	{
		if (!m_Model->set_field(widget.Id, field.Name, m_Pending->Value) && field.Name == "Name")
		{
			m_Error = "That name is empty, has a '/', or is taken by a sibling.";
		}
		m_Pending.reset();
	}
}
