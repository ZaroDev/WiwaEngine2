#pragma once

#include <string>
#include <utility>
#include <vector>

#include <assets/asset_data.h>

#include "edit_history.h"

namespace wiwa
{
	// The document the Skeleton editor edits: a skeleton's joint names and sockets, and the history. No ImGui. Joints cannot be added, removed or
	// reordered (meshes index joints by position); only names change. A call that is refused changes nothing and makes no undo step, and neither
	// does setting a value to what it already is. Names are not empty, have no space at either end, are at most MAX_NAME_LENGTH long and are unique
	// (joint names among joints, socket names among sockets).
	class SkeletonEditModel
	{
	public:
		static constexpr size_t MAX_NAME_LENGTH = 64;

		explicit SkeletonEditModel(SkeletonData skeleton);

		const SkeletonData& data() const { return m_Skeleton; }

		// Renames joints at once, as one undo step: the names are checked as the final set, so two joints can swap names. Sockets follow their joint.
		// False, and why in error, if a name is not valid or two joints would share one.
		bool rename_joints(const std::vector<std::pair<size_t, std::string>>& renames, std::string& error);
		bool rename_joint(size_t joint, const std::string& name, std::string& error);

		// old name -> new name, for every joint whose name differs from what the model was made with. A joint renamed twice shows once, from the
		// original name to the last; two joints that swapped show as two entries.
		std::vector<std::pair<std::string, std::string>> renames() const;

		// SIZE_MAX if refused (no such joint).
		size_t add_socket(const std::string& joint);
		bool remove_socket(size_t socket);
		bool rename_socket(size_t socket, const std::string& name, std::string& error);
		bool set_socket_joint(size_t socket, const std::string& joint);
		// A rotation that is not a finite non-zero quaternion, or a number that is not finite, is refused; the rotation is normalized.
		bool set_socket_transform(size_t socket, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);

		// A drag (a gizmo, a number field being dragged) changes a socket many times: between begin_gesture and end_gesture the changes make one undo step.
		void begin_gesture();
		void end_gesture();

		void undo();
		void redo();
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		bool dirty() const { return m_History.dirty(); }
		// What was saved is the new baseline of renames().
		void mark_saved();

		static bool valid_name(const std::string& name);

	private:
		// A real change: one undo step, or part of the gesture that is going on.
		void record(SkeletonData before);

		SkeletonData m_Skeleton;
		std::vector<std::string> m_OriginalNames;
		EditHistory<SkeletonData> m_History;
		bool m_InGesture = false;
		bool m_GestureChanged = false;
		SkeletonData m_GestureBefore{};
	};
}
