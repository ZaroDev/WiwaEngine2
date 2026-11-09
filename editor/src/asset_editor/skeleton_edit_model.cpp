#include <editor_common.h>
#include "skeleton_edit_model.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	}

	SkeletonEditModel::SkeletonEditModel(SkeletonData skeleton) : m_Skeleton(std::move(skeleton)), m_OriginalNames(m_Skeleton.JointNames) {}

	bool SkeletonEditModel::valid_name(const std::string& name)
	{
		return !name.empty() && name.size() <= MAX_NAME_LENGTH && name.front() != ' ' && name.back() != ' ' && name.find('\t') == std::string::npos;
	}

	bool SkeletonEditModel::rename_joints(const std::vector<std::pair<size_t, std::string>>& renames, std::string& error)
	{
		error.clear();
		std::vector<std::string> names = m_Skeleton.JointNames;
		for (const auto& [joint, name] : renames)
		{
			if (joint >= names.size())
			{
				error = "there is no such joint";
				return false;
			}
			if (!valid_name(name))
			{
				error = "'" + name + "' is not a valid name (not empty, no space at either end, at most 64 characters)";
				return false;
			}
			names[joint] = name;
		}
		std::unordered_set<std::string> seen;
		for (const std::string& name : names)
		{
			if (!seen.insert(name).second)
			{
				error = "the name '" + name + "' would be used by two joints";
				return false;
			}
		}
		if (names == m_Skeleton.JointNames)
		{
			return true; // nothing changes
		}

		SkeletonData before = m_Skeleton;
		std::unordered_map<std::string, std::string> mapping; // all at once, so a swap works
		for (size_t i = 0; i < names.size(); i++)
		{
			mapping[m_Skeleton.JointNames[i]] = names[i];
		}
		for (SkeletonSocket& socket : m_Skeleton.Sockets)
		{
			socket.Joint = mapping[socket.Joint];
		}
		m_Skeleton.JointNames = std::move(names);
		record(std::move(before));
		return true;
	}

	bool SkeletonEditModel::rename_joint(size_t joint, const std::string& name, std::string& error)
	{
		return rename_joints({ { joint, name } }, error);
	}

	std::vector<std::pair<std::string, std::string>> SkeletonEditModel::renames() const
	{
		std::vector<std::pair<std::string, std::string>> out;
		for (size_t i = 0; i < m_Skeleton.JointNames.size() && i < m_OriginalNames.size(); i++)
		{
			if (m_Skeleton.JointNames[i] != m_OriginalNames[i])
			{
				out.emplace_back(m_OriginalNames[i], m_Skeleton.JointNames[i]);
			}
		}
		return out;
	}

	size_t SkeletonEditModel::add_socket(const std::string& joint)
	{
		if (std::find(m_Skeleton.JointNames.begin(), m_Skeleton.JointNames.end(), joint) == m_Skeleton.JointNames.end())
		{
			return SIZE_MAX;
		}
		std::string name = "Socket";
		for (int number = 2; std::any_of(m_Skeleton.Sockets.begin(), m_Skeleton.Sockets.end(), [&](const SkeletonSocket& s) { return s.Name == name; }); number++)
		{
			name = "Socket " + std::to_string(number);
		}
		SkeletonData before = m_Skeleton;
		SkeletonSocket socket;
		socket.Name = name;
		socket.Joint = joint;
		m_Skeleton.Sockets.push_back(std::move(socket));
		record(std::move(before));
		return m_Skeleton.Sockets.size() - 1;
	}

	bool SkeletonEditModel::remove_socket(size_t socket)
	{
		if (socket >= m_Skeleton.Sockets.size())
		{
			return false;
		}
		SkeletonData before = m_Skeleton;
		m_Skeleton.Sockets.erase(m_Skeleton.Sockets.begin() + static_cast<std::ptrdiff_t>(socket));
		record(std::move(before));
		return true;
	}

	bool SkeletonEditModel::rename_socket(size_t socket, const std::string& name, std::string& error)
	{
		error.clear();
		if (socket >= m_Skeleton.Sockets.size())
		{
			error = "there is no such socket";
			return false;
		}
		if (!valid_name(name))
		{
			error = "'" + name + "' is not a valid name (not empty, no space at either end, at most 64 characters)";
			return false;
		}
		for (size_t i = 0; i < m_Skeleton.Sockets.size(); i++)
		{
			if (i != socket && m_Skeleton.Sockets[i].Name == name)
			{
				error = "the name '" + name + "' is used by another socket";
				return false;
			}
		}
		if (m_Skeleton.Sockets[socket].Name == name)
		{
			return true;
		}
		SkeletonData before = m_Skeleton;
		m_Skeleton.Sockets[socket].Name = name;
		record(std::move(before));
		return true;
	}

	bool SkeletonEditModel::set_socket_joint(size_t socket, const std::string& joint)
	{
		if (socket >= m_Skeleton.Sockets.size()
			|| std::find(m_Skeleton.JointNames.begin(), m_Skeleton.JointNames.end(), joint) == m_Skeleton.JointNames.end())
		{
			return false;
		}
		if (m_Skeleton.Sockets[socket].Joint == joint)
		{
			return true;
		}
		SkeletonData before = m_Skeleton;
		m_Skeleton.Sockets[socket].Joint = joint;
		record(std::move(before));
		return true;
	}

	bool SkeletonEditModel::set_socket_transform(size_t socket, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
	{
		const float length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
		if (socket >= m_Skeleton.Sockets.size() || !finite(position) || !finite(scale) || !std::isfinite(length) || !(length > 0.0f))
		{
			return false;
		}
		const glm::quat unit = glm::normalize(rotation);
		SkeletonSocket& target = m_Skeleton.Sockets[socket];
		if (target.Position == position && target.Rotation == unit && target.Scale == scale)
		{
			return true;
		}
		SkeletonData before = m_Skeleton;
		target.Position = position;
		target.Rotation = unit;
		target.Scale = scale;
		record(std::move(before));
		return true;
	}

	void SkeletonEditModel::record(SkeletonData before)
	{
		if (m_InGesture)
		{
			m_GestureChanged = true;
			return;
		}
		m_History.push(std::move(before));
	}

	void SkeletonEditModel::begin_gesture()
	{
		if (!m_InGesture)
		{
			m_InGesture = true;
			m_GestureChanged = false;
			m_GestureBefore = m_Skeleton;
		}
	}

	void SkeletonEditModel::end_gesture()
	{
		if (m_InGesture)
		{
			m_InGesture = false;
			if (m_GestureChanged)
			{
				m_History.push(std::move(m_GestureBefore));
			}
			m_GestureBefore = SkeletonData{};
		}
	}

	void SkeletonEditModel::mark_saved()
	{
		m_History.mark_saved();
		m_OriginalNames = m_Skeleton.JointNames;
	}

	void SkeletonEditModel::undo()
	{
		if (m_History.can_undo())
		{
			m_Skeleton = m_History.undo(m_Skeleton);
		}
	}

	void SkeletonEditModel::redo()
	{
		if (m_History.can_redo())
		{
			m_Skeleton = m_History.redo(m_Skeleton);
		}
	}
}
