#include <editor_common.h>
#include "ui_edit_model.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }

		bool finite_value(const FieldValue& value)
		{
			return std::visit([](const auto& v) -> bool
				{
					using T = std::decay_t<decltype(v)>;
					if constexpr (std::is_same_v<T, float>)
					{
						return std::isfinite(v);
					}
					else if constexpr (std::is_same_v<T, glm::vec2>)
					{
						return finite(v);
					}
					else if constexpr (std::is_same_v<T, glm::vec3>)
					{
						return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
					}
					else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
					{
						return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
					}
					else
					{
						return true;
					}
				}, value);
		}

		bool same_widget(const UiWidget& a, const UiWidget& b)
		{
			if (a.Id != b.Id || a.Parent != b.Parent || a.Kind != b.Kind)
			{
				return false;
			}
			for (const UiField& field : ui_fields())
			{
				if (field.Get(a) != field.Get(b))
				{
					return false;
				}
			}
			return true;
		}

		bool same_panel(const UiPanelData& a, const UiPanelData& b)
		{
			if (a.ReferenceResolution != b.ReferenceResolution || a.MatchWidthOrHeight != b.MatchWidthOrHeight || a.Widgets.size() != b.Widgets.size())
			{
				return false;
			}
			for (size_t i = 0; i < a.Widgets.size(); i++)
			{
				if (!same_widget(a.Widgets[i], b.Widgets[i]))
				{
					return false;
				}
			}
			return true;
		}

		// What a widget of a kind starts as: a size that can be seen and a look that shows what it is.
		UiWidget make_widget(WidgetKind kind)
		{
			UiWidget widget;
			widget.Kind = kind;
			widget.Name = widget_kind_name(kind);
			widget.OffsetMin = { 0.0f, 0.0f };

			switch (kind)
			{
			case WidgetKind::Panel:
				widget.OffsetMax = { 200.0f, 200.0f };
				widget.Color = { 0.1f, 0.1f, 0.12f, 0.6f };
				break;
			case WidgetKind::HStack:
			case WidgetKind::VStack:
				widget.OffsetMax = { 300.0f, 300.0f };
				widget.Spacing = 8.0f;
				widget.Padding = { 8.0f, 8.0f, 8.0f, 8.0f };
				widget.Color = { 0.0f, 0.0f, 0.0f, 0.0f };
				break;
			case WidgetKind::Text:
				widget.OffsetMax = { 200.0f, 32.0f };
				widget.Text = "Text";
				break;
			case WidgetKind::Image:
				widget.OffsetMax = { 128.0f, 128.0f };
				break;
			case WidgetKind::Button:
				widget.OffsetMax = { 160.0f, 40.0f };
				widget.Color = { 0.25f, 0.25f, 0.3f, 1.0f };
				break;
			case WidgetKind::Slider:
				widget.OffsetMax = { 220.0f, 24.0f };
				widget.Color = { 0.3f, 0.6f, 1.0f, 1.0f };
				break;
			case WidgetKind::Toggle:
				widget.OffsetMax = { 32.0f, 32.0f };
				widget.Color = { 0.3f, 0.8f, 0.4f, 1.0f };
				break;
			case WidgetKind::Bar:
				widget.OffsetMax = { 220.0f, 24.0f };
				widget.Value = 0.5f;
				widget.Color = { 0.3f, 0.8f, 0.4f, 1.0f };
				break;
			default:
				widget.OffsetMax = { 100.0f, 30.0f };
				break;
			}
			return widget;
		}
	}

	UiEditModel::UiEditModel(UiPanelData data) : m_Data(std::move(data))
	{
		UiId highest = 0;
		for (const UiWidget& widget : m_Data.Widgets)
		{
			highest = std::max(highest, widget.Id);
		}
		m_NextId = std::max({ m_Data.NextId, highest + 1, 1u });
		m_Data.NextId = m_NextId;
	}

	void UiEditModel::commit(Snapshot before)
	{
		m_Undo.push_back(std::move(before));
		while (m_Undo.size() > MAX_UNDO_STEPS)
		{
			m_Undo.pop_front();
		}
		m_Redo.clear();
		m_Revision = ++m_RevisionCounter;
	}

	void UiEditModel::restore(Snapshot state)
	{
		m_Data = std::move(state.Data);
		m_Revision = state.Revision;
		m_Data.NextId = std::max(m_Data.NextId, m_NextId);
	}

	void UiEditModel::undo()
	{
		if (m_Undo.empty())
		{
			return;
		}
		Snapshot previous = std::move(m_Undo.back());
		m_Undo.pop_back();
		m_Redo.push_back(snapshot());
		restore(std::move(previous));
	}

	void UiEditModel::redo()
	{
		if (m_Redo.empty())
		{
			return;
		}
		Snapshot next = std::move(m_Redo.back());
		m_Redo.pop_back();
		m_Undo.push_back(snapshot());
		restore(std::move(next));
	}

	std::string UiEditModel::unique_name(UiId parent, const std::string& base, UiId ignore) const
	{
		std::unordered_set<std::string> taken;
		for (const UiWidget& widget : m_Data.Widgets)
		{
			if (widget.Parent == parent && widget.Id != ignore)
			{
				taken.insert(widget.Name);
			}
		}
		if (!taken.contains(base))
		{
			return base;
		}
		for (u32 n = 1;; n++)
		{
			std::string candidate = base + " " + std::to_string(n);
			if (!taken.contains(candidate))
			{
				return candidate;
			}
		}
	}

	std::vector<UiId> UiEditModel::subtree_of(UiId id) const
	{
		std::vector<UiId> ids{ id };
		std::unordered_set<UiId> seen{ id };
		for (size_t i = 0; i < ids.size(); i++)
		{
			for (const UiWidget& widget : m_Data.Widgets)
			{
				if (widget.Parent == ids[i] && seen.insert(widget.Id).second)
				{
					ids.push_back(widget.Id);
				}
			}
		}
		return ids;
	}

	bool UiEditModel::is_descendant(UiId candidate, UiId ancestor) const
	{
		UiId current = candidate;
		for (size_t steps = 0; current != 0 && steps <= m_Data.Widgets.size(); steps++)
		{
			if (current == ancestor)
			{
				return true;
			}
			const UiWidget* widget = m_Data.find(current);
			current = widget != nullptr ? widget->Parent : 0;
		}
		return false;
	}

	UiId UiEditModel::add(WidgetKind kind, UiId parent)
	{
		if (parent != 0 && m_Data.find(parent) == nullptr)
		{
			return 0;
		}

		Snapshot before = snapshot();
		UiWidget widget = make_widget(kind);
		widget.Id = m_NextId++;
		widget.Parent = parent;
		widget.Name = unique_name(parent, widget.Name, 0);
		m_Data.NextId = m_NextId;
		m_Data.Widgets.push_back(std::move(widget));
		commit(std::move(before));
		return m_Data.Widgets.back().Id;
	}

	bool UiEditModel::remove(UiId id)
	{
		if (id == 0 || m_Data.find(id) == nullptr)
		{
			return false;
		}

		Snapshot before = snapshot();
		const std::vector<UiId> doomed = subtree_of(id);
		std::erase_if(m_Data.Widgets, [&](const UiWidget& widget) { return std::find(doomed.begin(), doomed.end(), widget.Id) != doomed.end(); });
		commit(std::move(before));
		return true;
	}

	bool UiEditModel::rename(UiId id, const std::string& name)
	{
		UiWidget* widget = m_Data.find(id);
		if (widget == nullptr || name.empty() || name.find('/') != std::string::npos)
		{
			return false;
		}
		if (widget->Name == name)
		{
			return true;
		}
		if (unique_name(widget->Parent, name, id) != name)
		{
			return false; // a sibling has it
		}

		Snapshot before = snapshot();
		widget->Name = name;
		commit(std::move(before));
		return true;
	}

	bool UiEditModel::reparent(UiId id, UiId newParent, size_t index)
	{
		if (m_Data.find(id) == nullptr || (newParent != 0 && m_Data.find(newParent) == nullptr) || is_descendant(newParent, id))
		{
			return false;
		}

		Snapshot before = snapshot();
		const auto at = std::find_if(m_Data.Widgets.begin(), m_Data.Widgets.end(), [&](const UiWidget& widget) { return widget.Id == id; });
		UiWidget moved = std::move(*at);
		m_Data.Widgets.erase(at);

		moved.Parent = newParent;
		moved.Name = unique_name(newParent, moved.Name, 0);

		// before the sibling that is at that position now, or after the last one
		size_t position = m_Data.Widgets.size();
		size_t siblings = 0;
		size_t lastSibling = 0;
		bool found = false;
		for (size_t i = 0; i < m_Data.Widgets.size(); i++)
		{
			if (m_Data.Widgets[i].Parent == newParent)
			{
				if (siblings == index)
				{
					position = i;
					found = true;
					break;
				}
				siblings++;
				lastSibling = i;
			}
		}
		if (!found && siblings > 0)
		{
			position = lastSibling + 1;
		}
		m_Data.Widgets.insert(m_Data.Widgets.begin() + static_cast<std::ptrdiff_t>(position), std::move(moved));

		if (same_panel(before.Data, m_Data))
		{
			m_Data = std::move(before.Data); // dropped where it was: not a step
			return true;
		}
		commit(std::move(before));
		return true;
	}

	UiId UiEditModel::duplicate(UiId id)
	{
		const UiWidget* original = m_Data.find(id);
		if (id == 0 || original == nullptr)
		{
			return 0;
		}

		Snapshot before = snapshot();
		const std::vector<UiId> ids = subtree_of(id);
		std::unordered_map<UiId, UiId> renamed;
		for (const UiId old : ids)
		{
			renamed[old] = m_NextId++;
		}

		std::vector<UiWidget> copies;
		for (const UiWidget& widget : m_Data.Widgets) // in the order of the panel, so siblings keep theirs
		{
			if (!renamed.contains(widget.Id))
			{
				continue;
			}
			UiWidget copy = widget;
			copy.Id = renamed[widget.Id];
			copy.Parent = widget.Id == id ? widget.Parent : renamed[widget.Parent];
			if (widget.Id == id)
			{
				copy.Name = unique_name(widget.Parent, widget.Name, 0);
			}
			copies.push_back(std::move(copy));
		}
		// the copy of the widget first, then its descendants
		std::stable_partition(copies.begin(), copies.end(), [&](const UiWidget& copy) { return copy.Id == renamed[id]; });

		const auto at = std::find_if(m_Data.Widgets.begin(), m_Data.Widgets.end(), [&](const UiWidget& widget) { return widget.Id == id; });
		m_Data.Widgets.insert(at + 1, copies.begin(), copies.end());
		m_Data.NextId = m_NextId;
		commit(std::move(before));
		return renamed[id];
	}

	bool UiEditModel::set_field(UiId id, std::string_view field, const FieldValue& value)
	{
		UiWidget* widget = m_Data.find(id);
		if (widget == nullptr)
		{
			return false;
		}
		if (field == "Name")
		{
			const std::string* name = std::get_if<std::string>(&value);
			return name != nullptr && rename(id, *name);
		}

		const UiField* descriptor = find_ui_field(field);
		if (descriptor == nullptr || !finite_value(value))
		{
			return false;
		}

		UiWidget changed = *widget;
		if (!descriptor->Set(changed, value))
		{
			return false;
		}
		if (descriptor->Get(changed) == descriptor->Get(*widget))
		{
			return true;
		}

		Snapshot before = snapshot();
		*widget = std::move(changed);
		commit(std::move(before));
		return true;
	}

	bool UiEditModel::set_rect(UiId id, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& offsetMin, const glm::vec2& offsetMax)
	{
		UiWidget* widget = m_Data.find(id);
		if (widget == nullptr || !finite(anchorMin) || !finite(anchorMax) || !finite(offsetMin) || !finite(offsetMax))
		{
			return false;
		}
		if (widget->AnchorMin == anchorMin && widget->AnchorMax == anchorMax && widget->OffsetMin == offsetMin && widget->OffsetMax == offsetMax)
		{
			return true;
		}

		Snapshot before = snapshot();
		widget->AnchorMin = anchorMin;
		widget->AnchorMax = anchorMax;
		widget->OffsetMin = offsetMin;
		widget->OffsetMax = offsetMax;
		commit(std::move(before));
		return true;
	}

	bool UiEditModel::set_reference(const glm::vec2& resolution, float match)
	{
		if (!finite(resolution) || !(resolution.x > 0.0f) || !(resolution.y > 0.0f) || !std::isfinite(match) || match < 0.0f || match > 1.0f)
		{
			return false;
		}
		if (m_Data.ReferenceResolution == resolution && m_Data.MatchWidthOrHeight == match)
		{
			return true;
		}

		Snapshot before = snapshot();
		m_Data.ReferenceResolution = resolution;
		m_Data.MatchWidthOrHeight = match;
		commit(std::move(before));
		return true;
	}
}
