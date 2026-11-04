#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <ui/ui_widget.h>

namespace wiwa
{
	// The document the UI editor edits: a panel and its history. No ImGui and no GPU, so every rule of editing is tested without a window.
	// Every call that changes the panel is one undo step; a call that is refused (it returns false, or 0 for an id) changes nothing and
	// makes no step, and neither does setting a value to what it already is.
	class UiEditModel
	{
	public:
		static constexpr size_t MAX_UNDO_STEPS = 100;

		explicit UiEditModel(UiPanelData data);

		const UiPanelData& data() const { return m_Data; }

		// A new widget of this kind (with a size you can see) as the last child of parent (0 for the top level). Its name is the name of
		// the kind, with a number if a sibling has it. 0 if the parent does not exist.
		UiId add(WidgetKind kind, UiId parent);

		// The widget and everything under it.
		bool remove(UiId id);

		// Names are unique among siblings, not empty, and have no '/' (scripts find widgets by the path of names).
		bool rename(UiId id, const std::string& name);

		// Moves the widget, with everything under it, to be child number `index` of newParent (past the end is the end). Refused if the
		// parent is the widget or one of its descendants. A name that the new siblings have is made unique.
		bool reparent(UiId id, UiId newParent, size_t index);

		// A copy of the widget and everything under it, right after the widget, with new ids and a free name. Returns its id (0 if none).
		UiId duplicate(UiId id);

		// Sets one field (see ui_fields()). Name goes through rename. Refused: a field that does not exist, a value of another type or
		// out of its names, a number that is not finite.
		bool set_field(UiId id, std::string_view field, const FieldValue& value);

		// The four edges of the rect in one step (the canvas handles). Refused for numbers that are not finite.
		bool set_rect(UiId id, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& offsetMin, const glm::vec2& offsetMax);

		// The reference resolution (both sides above 0) and how the canvas scales to it (0 to 1).
		bool set_reference(const glm::vec2& resolution, float match);

		void undo();
		void redo();
		bool can_undo() const { return !m_Undo.empty(); }
		bool can_redo() const { return !m_Redo.empty(); }

		// Whether the panel differs from what was last saved (undoing back to it is clean again).
		bool dirty() const { return m_Revision != m_SavedRevision; }
		void mark_saved() { m_SavedRevision = m_Revision; }

	private:
		struct Snapshot
		{
			UiPanelData Data{};
			u64 Revision = 0;
		};

		// Records the state before a change as an undo step; call it with that state after the change was made.
		void commit(Snapshot before);
		Snapshot snapshot() const { return Snapshot{ m_Data, m_Revision }; }
		void restore(Snapshot state);

		std::string unique_name(UiId parent, const std::string& base, UiId ignore) const;
		std::vector<UiId> subtree_of(UiId id) const; // the widget first, then everything under it
		bool is_descendant(UiId candidate, UiId ancestor) const;

		UiPanelData m_Data{};
		UiId m_NextId = 1; // never goes down, so an id that was undone is not handed out again
		std::deque<Snapshot> m_Undo{};
		std::vector<Snapshot> m_Redo{};
		u64 m_Revision = 0;
		u64 m_RevisionCounter = 0;
		u64 m_SavedRevision = 0;
	};
}
