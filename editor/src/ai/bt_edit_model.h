#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <ai/bt_file.h>

#include "../asset_editor/edit_history.h"

namespace wiwa
{
	// The document the behaviour tree editor edits: a BehaviorTreeDoc and its history. No ImGui. Every call that changes the tree is one undo step (or part
	// of the gesture going on); a call that is refused (it returns false / an empty id, and error() says why) changes nothing and makes no step, and neither
	// does a call that would change nothing. The tree stays a tree: a node is never made the child of its own descendant, a leaf gets no children, a
	// decorator no second one. Node ids are never reused in the life of the model (not even after an undo). Problems that are only about what a parameter
	// holds are not refused here: problems() (validate_bt) lists them, and Save refuses while one is an error.
	class BtEditModel
	{
	public:
		explicit BtEditModel(BehaviorTreeDoc doc);

		const BehaviorTreeDoc& doc() const { return m_Doc; }
		const std::string& error() const { return m_Error; }
		void clear_error() { m_Error.clear(); }

		// Makes a node of a built-in or Lua leaf type under `parent` at `index` (clamped to the end). An empty parent is allowed only for the first node of
		// an empty tree, which becomes the root. Returns the new id, or an empty string if it was refused.
		std::string add_node(const std::string& type, const std::string& parent, size_t index = static_cast<size_t>(-1));
		// Removes the node and everything below it. The root cannot be removed (replace it by removing its children, or start another tree).
		bool remove_node(const std::string& id);
		// Moves a node (with what is below it) under newParent at index. Refused for the root, for a move into the node's own subtree and for a parent that
		// is a leaf or has no room.
		bool move_node(const std::string& id, const std::string& newParent, size_t index = static_cast<size_t>(-1));
		bool rename_node(const std::string& id, const std::string& name);
		// A parameter of a node; null removes it. The editor's layout lives in the "ui" parameter ({ "x": .., "y": .. }).
		bool set_param(const std::string& id, const std::string& key, const nlohmann::json& value);

		// Blackboard keys. A key a node uses cannot be retyped or removed unless `force` (then the parameters that name it are cleared).
		bool add_key(const std::string& key, BtValueType type, const nlohmann::json& fallback = nlohmann::json());
		// Rewrites every node parameter that names the key (key params and "key:<name>" targets).
		bool rename_key(const std::string& key, const std::string& name);
		bool retype_key(const std::string& key, BtValueType type, bool force = false);
		bool set_key_default(const std::string& key, const nlohmann::json& fallback);
		bool remove_key(const std::string& key, bool force = false);
		// The ids of the nodes whose parameters name the key.
		std::vector<std::string> users_of_key(const std::string& key) const;

		std::vector<BtProblem> problems() const { return validate_bt(m_Doc); }

		// One undo step for several calls: begin, make the changes, end (nested begins count).
		void begin_gesture();
		void end_gesture();
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		bool undo();
		bool redo();

		// Writes the .wbt through a temp file and a rename. Refused while problems() has an error (the importer would refuse the file).
		bool save(const std::filesystem::path& path);
		bool dirty() const { return m_History.dirty(); }

	private:
		bool refuse(const std::string& why);
		// Records `before` as an undo step unless a gesture is going on (then at its end).
		void commit(BehaviorTreeDoc before);
		BtNodeDesc* find(const std::string& id);
		BtNodeDesc* parent_of(const std::string& id);
		bool in_subtree(const std::string& root, const std::string& id) const;
		std::string new_id();

		BehaviorTreeDoc m_Doc{};
		std::string m_Error{};
		EditHistory<BehaviorTreeDoc> m_History{};
		u32 m_NextId = 1;
		int m_Gesture = 0;
		bool m_GestureChanged = false;
		BehaviorTreeDoc m_GestureBefore{};
	};
}
