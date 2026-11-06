#pragma once

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <assets/asset_data.h>

namespace wiwa
{
	// One clip of a model as the events editor sees it.
	struct EditableClip
	{
		std::string Name{};
		float Duration = 0.0f;
		std::vector<AnimationEvent> Events{}; // sorted by time
	};

	// The document the Animation panel edits: the events of the clips of one model, and their history. No ImGui, so the rules of editing are
	// tested without a window. Every call that changes the document is one undo step; a call that is refused (it returns false, or SIZE_MAX for
	// an index) changes nothing and makes no step, and neither does setting a value to what it already is. Events are named by their clip and
	// their index in time order. Names are not empty and have no space at either end; times are numbers inside the clip.
	//
	// The events live in the "animationEvents" list of the model's import settings ({ clip, time, name }); the model importer merges them into the
	// clips, so saving them re-imports the model.
	class AnimationEditModel
	{
	public:
		static constexpr size_t MAX_UNDO_STEPS = 100;
		static constexpr size_t MAX_NAME_LENGTH = 64;

		explicit AnimationEditModel(std::vector<EditableClip> clips);
		// clips: name and duration of each clip of the model; the events come from settings (entries that are not usable, and those of a clip
		// the model does not have, are left out).
		static AnimationEditModel from_settings(const std::vector<std::pair<std::string, float>>& clips, const nlohmann::json& settings);

		const std::vector<EditableClip>& clips() const { return m_Clips; }

		// The index of the event in its clip afterwards, SIZE_MAX if refused.
		size_t add_event(size_t clip, float time, const std::string& name);
		size_t move_event(size_t clip, size_t event, float time);
		bool rename_event(size_t clip, size_t event, const std::string& name);
		bool remove_event(size_t clip, size_t event);

		void undo();
		void redo();
		bool can_undo() const { return !m_Undo.empty(); }
		bool can_redo() const { return !m_Redo.empty(); }

		// Whether the document differs from what was last saved (undoing back to it is clean again).
		bool dirty() const { return m_Revision != m_SavedRevision; }
		void mark_saved() { m_SavedRevision = m_Revision; }

		// Puts the events in settings["animationEvents"] and leaves every other setting alone; with no events the key is removed.
		void write_to(nlohmann::json& settings) const;

	private:
		struct Snapshot
		{
			std::vector<EditableClip> Clips{};
			u64 Revision = 0;
		};

		Snapshot snapshot() const { return Snapshot{ m_Clips, m_Revision }; }
		void commit(Snapshot before);
		void restore(Snapshot state);
		static bool valid_name(const std::string& name);
		bool valid_time(size_t clip, float time) const;
		size_t insert_sorted(size_t clip, AnimationEvent event);

		std::vector<EditableClip> m_Clips{};
		std::deque<Snapshot> m_Undo{};
		std::deque<Snapshot> m_Redo{};
		u64 m_Revision = 0;
		u64 m_SavedRevision = 0;
		u64 m_NextRevision = 1;
	};
}
