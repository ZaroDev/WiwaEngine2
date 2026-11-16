#include <editor_common.h>
#include "clip_edit_model.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "clip_math.h"

namespace wiwa
{
	namespace
	{
		constexpr float TIME_EPSILON = 1e-5f;

		bool is_key_channel(const AnimationChannel& channel) { return channel.Path != AnimationPath::Weights; }

		bool finite_values(std::span<const float> values)
		{
			return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
		}

		u32 components_for(AnimationPath path) { return path == AnimationPath::Rotation ? 4u : 3u; }

		// Where a key of a channel is, by time, after keys were put in: the index of the key at that time.
		size_t index_at(const AnimationChannel& channel, float time)
		{
			const auto it = std::lower_bound(channel.Times.begin(), channel.Times.end(), time - TIME_EPSILON);
			return static_cast<size_t>(it - channel.Times.begin());
		}
	}

	void normalize_selection(KeySelection& selection)
	{
		std::sort(selection.begin(), selection.end());
		selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
	}

	ClipEditModel::ClipEditModel(AnimationClipData clip) : m_Clip(std::move(clip)) {}

	bool ClipEditModel::valid_name(const std::string& name)
	{
		return !name.empty() && name.size() <= MAX_NAME_LENGTH && name.front() != ' ' && name.back() != ' ' && name.find('\t') == std::string::npos;
	}

	bool ClipEditModel::refuse(const std::string& why)
	{
		m_Error = why;
		return false;
	}

	void ClipEditModel::commit(AnimationClipData edited)
	{
		if (m_InGesture)
		{
			m_GestureChanged = true;
			m_Clip = std::move(edited);
			return;
		}
		AnimationClipData before = std::move(m_Clip);
		const size_t bytes = clip_bytes(before);
		m_History.push(std::move(before), bytes);
		m_Clip = std::move(edited);
	}

	void ClipEditModel::begin_gesture()
	{
		if (!m_InGesture)
		{
			m_InGesture = true;
			m_GestureChanged = false;
			m_GestureBefore = m_Clip;
		}
	}

	void ClipEditModel::end_gesture()
	{
		if (m_InGesture)
		{
			m_InGesture = false;
			if (m_GestureChanged)
			{
				const size_t bytes = clip_bytes(m_GestureBefore);
				m_History.push(std::move(m_GestureBefore), bytes);
			}
			m_GestureBefore = AnimationClipData{};
		}
	}

	void ClipEditModel::undo()
	{
		if (m_History.can_undo())
		{
			AnimationClipData current = std::move(m_Clip);
			const size_t bytes = clip_bytes(current);
			m_Clip = m_History.undo(std::move(current), bytes);
		}
	}

	void ClipEditModel::redo()
	{
		if (m_History.can_redo())
		{
			AnimationClipData current = std::move(m_Clip);
			const size_t bytes = clip_bytes(current);
			m_Clip = m_History.redo(std::move(current), bytes);
		}
	}

	std::optional<size_t> ClipEditModel::find_channel(const std::string& joint, AnimationPath path) const
	{
		for (size_t i = 0; i < m_Clip.Channels.size(); i++)
		{
			if (m_Clip.Channels[i].Joint == joint && m_Clip.Channels[i].Path == path)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	bool ClipEditModel::valid_selection(const KeySelection& selection) const
	{
		for (const KeyRef& key : selection)
		{
			if (key.Channel >= m_Clip.Channels.size() || key.Key >= m_Clip.Channels[key.Channel].Times.size() || !is_key_channel(m_Clip.Channels[key.Channel]))
			{
				return false;
			}
		}
		return true;
	}

	size_t ClipEditModel::make_channel(AnimationClipData& clip, const std::string& joint, AnimationPath path) const
	{
		AnimationChannel channel;
		channel.Joint = joint;
		channel.Path = path;
		channel.Interpolation = AnimationInterpolation::Linear;
		channel.Components = components_for(path);
		clip.Channels.push_back(std::move(channel));
		return clip.Channels.size() - 1;
	}

	// The selected keys get the time map(time) (inside the clip); keys of their channel they land on are replaced.
	bool ClipEditModel::remap_keys(const KeySelection& selection, const std::function<float(float)>& map, KeySelection* moved)
	{
		if (moved != nullptr)
		{
			moved->clear();
		}
		KeySelection keys = selection;
		normalize_selection(keys);
		if (keys.empty())
		{
			return true;
		}
		if (!valid_selection(keys))
		{
			return refuse("the selection names a key that does not exist");
		}

		AnimationClipData edited = m_Clip;
		KeySelection result;
		bool changed = false;
		size_t at = 0;
		while (at < keys.size())
		{
			const size_t channelIndex = keys[at].Channel;
			size_t end = at;
			while (end < keys.size() && keys[end].Channel == channelIndex)
			{
				end++;
			}
			AnimationChannel& channel = edited.Channels[channelIndex];

			struct Item
			{
				float Time;
				glm::vec4 Value;
				glm::vec4 In;
				glm::vec4 Out;
				bool Selected;
			};
			std::vector<Item> items;
			std::vector<bool> selected(channel.Times.size(), false);
			for (size_t i = at; i < end; i++)
			{
				selected[keys[i].Key] = true;
			}
			std::vector<float> newTimes(channel.Times.size());
			for (size_t k = 0; k < channel.Times.size(); k++)
			{
				newTimes[k] = selected[k] ? std::clamp(map(channel.Times[k]), 0.0f, edited.Duration) : channel.Times[k];
				changed = changed || std::abs(newTimes[k] - channel.Times[k]) > 1e-7f;
			}
			for (size_t k = 0; k < channel.Times.size(); k++)
			{
				bool dropped = false;
				for (size_t other = 0; other < channel.Times.size() && !dropped; other++)
				{
					if (selected[other] && other != k && std::abs(newTimes[other] - newTimes[k]) <= TIME_EPSILON)
					{
						// an unselected key under a moved one goes; of two moved keys at one time the later one stays
						dropped = !selected[k] || other > k;
					}
				}
				if (dropped)
				{
					changed = true;
					continue;
				}
				items.push_back(Item{ newTimes[k], key_value(channel, k), key_in_tangent(channel, k), key_out_tangent(channel, k), selected[k] });
			}
			std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Time < b.Time; });

			channel.Times.clear();
			channel.Values.assign(items.size() * channel_stride(channel), 0.0f);
			for (size_t k = 0; k < items.size(); k++)
			{
				channel.Times.push_back(items[k].Time);
				wiwa::set_key_value(channel, k, items[k].Value);
				set_key_tangents(channel, k, items[k].In, items[k].Out);
				if (items[k].Selected)
				{
					result.push_back(KeyRef{ channelIndex, k });
				}
			}
			at = end;
		}

		if (changed)
		{
			commit(std::move(edited));
		}
		if (moved != nullptr)
		{
			*moved = std::move(result);
		}
		return true;
	}

	bool ClipEditModel::move_keys(const KeySelection& selection, float dt, KeySelection* moved)
	{
		m_Error.clear();
		if (!std::isfinite(dt))
		{
			return refuse("the time shift is not a number");
		}
		if (!valid_selection(selection))
		{
			return refuse("the selection names a key that does not exist");
		}
		float low = m_Clip.Duration;
		float high = 0.0f;
		for (const KeyRef& key : selection)
		{
			low = std::min(low, m_Clip.Channels[key.Channel].Times[key.Key]);
			high = std::max(high, m_Clip.Channels[key.Channel].Times[key.Key]);
		}
		if (!selection.empty())
		{
			dt = std::clamp(dt, -low, m_Clip.Duration - high); // the whole selection stays inside the clip
		}
		return remap_keys(selection, [dt](float t) { return t + dt; }, moved);
	}

	bool ClipEditModel::scale_keys(const KeySelection& selection, float about, float factor, KeySelection* moved)
	{
		m_Error.clear();
		if (!std::isfinite(about) || !std::isfinite(factor) || factor <= 0.0f)
		{
			return refuse("the scale has to be a number above zero");
		}
		return remap_keys(selection, [about, factor](float t) { return about + (t - about) * factor; }, moved);
	}

	bool ClipEditModel::set_key_value(KeyRef key, std::span<const float> value)
	{
		m_Error.clear();
		if (!valid_selection({ key }))
		{
			return refuse("there is no such key");
		}
		AnimationClipData edited = m_Clip;
		AnimationChannel& channel = edited.Channels[key.Channel];
		if (!finite_values(value))
		{
			return refuse("a value is not a number");
		}
		glm::vec4 stored(0.0f);
		if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
		{
			if (value.size() != 3)
			{
				return refuse("a rotation takes three angles");
			}
			glm::quat q = euler_degrees_to_quat(glm::vec3(value[0], value[1], value[2]));
			if (key.Key > 0)
			{
				const glm::vec4 p = key_value(channel, key.Key - 1);
				q = quat_continuous(q, glm::quat(p.w, p.x, p.y, p.z));
			}
			else if (channel.Times.size() > 1)
			{
				const glm::vec4 n = key_value(channel, 1);
				q = quat_continuous(q, glm::quat(n.w, n.x, n.y, n.z));
			}
			stored = glm::vec4(q.x, q.y, q.z, q.w);
		}
		else
		{
			if (value.size() != channel.Components)
			{
				return refuse("the value has the wrong number of components");
			}
			for (size_t i = 0; i < value.size(); i++)
			{
				stored[static_cast<int>(i)] = value[i];
			}
		}
		if (glm::all(glm::equal(stored, key_value(channel, key.Key))))
		{
			return true;
		}
		wiwa::set_key_value(channel, key.Key, stored);
		commit(std::move(edited));
		return true;
	}


	std::optional<KeyRef> ClipEditModel::insert_key(size_t channelIndex, float time)
	{
		m_Error.clear();
		if (channelIndex >= m_Clip.Channels.size() || !is_key_channel(m_Clip.Channels[channelIndex]) || !channel_valid(m_Clip.Channels[channelIndex]))
		{
			refuse("there is no such channel");
			return std::nullopt;
		}
		if (!std::isfinite(time) || time < 0.0f || time > m_Clip.Duration)
		{
			refuse("the time is outside the clip");
			return std::nullopt;
		}
		const AnimationChannel& source = m_Clip.Channels[channelIndex];
		const size_t existing = index_at(source, time);
		if (existing < source.Times.size() && std::abs(source.Times[existing] - time) <= TIME_EPSILON)
		{
			return KeyRef{ channelIndex, existing }; // there is a key there already
		}
		AnimationClipData edited = m_Clip;
		AnimationChannel& channel = edited.Channels[channelIndex];
		glm::vec4 value = sample_channel(channel, time);
		if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
		{
			value = glm::normalize(value);
		}
		const size_t index = insert_key_at(channel, time, value);
		commit(std::move(edited));
		return KeyRef{ channelIndex, index };
	}

	bool ClipEditModel::delete_keys(const KeySelection& selection)
	{
		m_Error.clear();
		KeySelection keys = selection;
		normalize_selection(keys);
		if (keys.empty())
		{
			return true;
		}
		if (!valid_selection(keys))
		{
			return refuse("the selection names a key that does not exist");
		}
		std::map<size_t, size_t> perChannel;
		for (const KeyRef& key : keys)
		{
			perChannel[key.Channel]++;
		}
		for (const auto& [channel, count] : perChannel)
		{
			if (count >= m_Clip.Channels[channel].Times.size())
			{
				return refuse("a channel cannot be left without keys: delete the whole channel's joint instead");
			}
		}
		AnimationClipData edited = m_Clip;
		for (auto it = keys.rbegin(); it != keys.rend(); ++it)
		{
			erase_key(edited.Channels[it->Channel], it->Key);
		}
		commit(std::move(edited));
		return true;
	}

	KeyClipboard ClipEditModel::copy_keys(const KeySelection& selection) const
	{
		KeyClipboard clipboard;
		KeySelection keys = selection;
		normalize_selection(keys);
		if (!valid_selection(keys))
		{
			return clipboard;
		}
		float first = 1e30f;
		for (const KeyRef& key : keys)
		{
			first = std::min(first, m_Clip.Channels[key.Channel].Times[key.Key]);
		}
		for (const KeyRef& key : keys)
		{
			const AnimationChannel& channel = m_Clip.Channels[key.Channel];
			clipboard.Items.push_back(KeyClipboard::Item{ channel.Joint, channel.Path, channel.Interpolation, channel.Times[key.Key] - first, key_value(channel, key.Key) });
		}
		return clipboard;
	}

	KeySelection ClipEditModel::paste_keys(const KeyClipboard& clipboard, float playhead, const std::string& targetJoint, const SkeletonData* skeleton)
	{
		m_Error.clear();
		KeySelection pasted;
		if (clipboard.empty() || !std::isfinite(playhead))
		{
			return pasted;
		}
		bool oneJoint = true;
		for (const KeyClipboard::Item& item : clipboard.Items)
		{
			oneJoint = oneJoint && item.Joint == clipboard.Items.front().Joint;
		}
		const auto joint_of = [&](const KeyClipboard::Item& item) { return oneJoint && !targetJoint.empty() ? targetJoint : item.Joint; };
		if (skeleton != nullptr)
		{
			for (const KeyClipboard::Item& item : clipboard.Items)
			{
				const std::string joint = joint_of(item);
				if (std::find(skeleton->JointNames.begin(), skeleton->JointNames.end(), joint) == skeleton->JointNames.end())
				{
					refuse("the skeleton has no joint '" + joint + "' to paste onto");
					return pasted;
				}
			}
		}

		AnimationClipData edited = m_Clip;
		std::vector<std::pair<size_t, float>> placed;
		for (const KeyClipboard::Item& item : clipboard.Items)
		{
			const float time = playhead + item.Time;
			if (time < 0.0f || time > edited.Duration)
			{
				continue; // does not fit in the clip
			}
			const std::string joint = joint_of(item);
			size_t channelIndex = edited.Channels.size();
			for (size_t i = 0; i < edited.Channels.size(); i++)
			{
				if (edited.Channels[i].Joint == joint && edited.Channels[i].Path == item.Path)
				{
					channelIndex = i;
					break;
				}
			}
			if (channelIndex == edited.Channels.size())
			{
				channelIndex = make_channel(edited, joint, item.Path);
			}
			AnimationChannel& channel = edited.Channels[channelIndex];
			if (!is_key_channel(channel) || (channel.Path == AnimationPath::Rotation && item.Value == glm::vec4(0.0f)))
			{
				continue;
			}
			insert_key_at(channel, time, item.Value);
			placed.emplace_back(channelIndex, time);
		}
		while (edited.Channels.size() > m_Clip.Channels.size() && edited.Channels.back().Times.empty())
		{
			edited.Channels.pop_back(); // a channel made for keys that did not fit
		}
		if (placed.empty())
		{
			return pasted;
		}
		for (const auto& [channelIndex, time] : placed)
		{
			pasted.push_back(KeyRef{ channelIndex, index_at(edited.Channels[channelIndex], time) });
		}
		normalize_selection(pasted);
		commit(std::move(edited));
		return pasted;
	}

	bool ClipEditModel::key_pose(const std::string& joint, float time, const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale, const SkeletonData* skeleton)
	{
		m_Error.clear();
		if (!std::isfinite(time) || time < 0.0f || time > m_Clip.Duration)
		{
			return refuse("the time is outside the clip");
		}
		const float rotationLength = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
		if (!std::isfinite(translation.x + translation.y + translation.z + scale.x + scale.y + scale.z) || !(rotationLength > 0.0f) || !std::isfinite(rotationLength))
		{
			return refuse("the pose has a value that is not a number");
		}
		size_t jointIndex = SIZE_MAX;
		if (skeleton != nullptr)
		{
			const auto it = std::find(skeleton->JointNames.begin(), skeleton->JointNames.end(), joint);
			if (it == skeleton->JointNames.end())
			{
				return refuse("the skeleton has no joint '" + joint + "'");
			}
			jointIndex = static_cast<size_t>(it - skeleton->JointNames.begin());
		}

		AnimationClipData edited = m_Clip;
		const glm::quat unit = glm::normalize(rotation);
		for (const AnimationPath path : { AnimationPath::Translation, AnimationPath::Rotation, AnimationPath::Scale })
		{
			size_t channelIndex = edited.Channels.size();
			for (size_t i = 0; i < edited.Channels.size(); i++)
			{
				if (edited.Channels[i].Joint == joint && edited.Channels[i].Path == path)
				{
					channelIndex = i;
					break;
				}
			}
			glm::vec4 value(0.0f);
			switch (path)
			{
			case AnimationPath::Translation: value = glm::vec4(translation, 0.0f); break;
			case AnimationPath::Scale: value = glm::vec4(scale, 0.0f); break;
			default: value = glm::vec4(unit.x, unit.y, unit.z, unit.w); break;
			}

			if (channelIndex == edited.Channels.size())
			{
				// a new channel: the rest pose until this key
				channelIndex = make_channel(edited, joint, path);
				if (time > 0.0f)
				{
					glm::vec4 rest(0.0f);
					if (path == AnimationPath::Rotation)
					{
						const glm::quat q = jointIndex < skeleton->RestRotation.size() ? skeleton->RestRotation[jointIndex] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
						rest = glm::vec4(q.x, q.y, q.z, q.w);
					}
					else if (path == AnimationPath::Translation)
					{
						rest = glm::vec4(skeleton != nullptr && jointIndex < skeleton->RestTranslation.size() ? skeleton->RestTranslation[jointIndex] : glm::vec3(0.0f), 0.0f);
					}
					else
					{
						rest = glm::vec4(skeleton != nullptr && jointIndex < skeleton->RestScale.size() ? skeleton->RestScale[jointIndex] : glm::vec3(1.0f), 0.0f);
					}
					insert_key_at(edited.Channels[channelIndex], 0.0f, rest);
				}
			}
			else if (path == AnimationPath::Rotation)
			{
				const glm::vec4 around = sample_channel(edited.Channels[channelIndex], time);
				const glm::quat q = quat_continuous(unit, glm::quat(around.w, around.x, around.y, around.z));
				value = glm::vec4(q.x, q.y, q.z, q.w);
			}
			insert_key_at(edited.Channels[channelIndex], time, value);
		}
		commit(std::move(edited));
		return true;
	}

	std::vector<glm::vec3> ClipEditModel::euler_values(size_t channelIndex) const
	{
		std::vector<glm::vec3> out;
		if (channelIndex >= m_Clip.Channels.size())
		{
			return out;
		}
		const AnimationChannel& channel = m_Clip.Channels[channelIndex];
		if (channel.Path != AnimationPath::Rotation || channel.Components != 4 || !channel_valid(channel))
		{
			return out;
		}
		glm::vec3 previous(0.0f);
		for (size_t k = 0; k < channel.Times.size(); k++)
		{
			const glm::vec4 v = key_value(channel, k);
			previous = quat_to_euler_degrees(glm::quat(v.w, v.x, v.y, v.z), previous);
			out.push_back(previous);
		}
		return out;
	}

	std::optional<size_t> ClipEditModel::add_event(float time, const std::string& name)
	{
		m_Error.clear();
		if (!valid_name(name))
		{
			refuse("an event name is not empty, has no space at either end and is at most 64 characters");
			return std::nullopt;
		}
		if (!std::isfinite(time) || time < 0.0f || time > m_Clip.Duration)
		{
			refuse("the time is outside the clip");
			return std::nullopt;
		}
		AnimationClipData edited = m_Clip;
		const auto after = std::upper_bound(edited.Events.begin(), edited.Events.end(), time, [](float t, const AnimationEvent& e) { return t < e.Time; });
		const size_t index = static_cast<size_t>(after - edited.Events.begin());
		edited.Events.insert(after, AnimationEvent{ time, name });
		commit(std::move(edited));
		return index;
	}

	std::optional<size_t> ClipEditModel::move_event(size_t event, float time)
	{
		m_Error.clear();
		if (event >= m_Clip.Events.size())
		{
			refuse("there is no such event");
			return std::nullopt;
		}
		if (!std::isfinite(time) || time < 0.0f || time > m_Clip.Duration)
		{
			refuse("the time is outside the clip");
			return std::nullopt;
		}
		if (m_Clip.Events[event].Time == time)
		{
			return event;
		}
		AnimationClipData edited = m_Clip;
		AnimationEvent moved = edited.Events[event];
		moved.Time = time;
		edited.Events.erase(edited.Events.begin() + static_cast<std::ptrdiff_t>(event));
		const auto after = std::upper_bound(edited.Events.begin(), edited.Events.end(), time, [](float t, const AnimationEvent& e) { return t < e.Time; });
		const size_t index = static_cast<size_t>(after - edited.Events.begin());
		edited.Events.insert(after, std::move(moved));
		commit(std::move(edited));
		return index;
	}

	bool ClipEditModel::rename_event(size_t event, const std::string& name)
	{
		m_Error.clear();
		if (event >= m_Clip.Events.size())
		{
			return refuse("there is no such event");
		}
		if (!valid_name(name))
		{
			return refuse("an event name is not empty, has no space at either end and is at most 64 characters");
		}
		if (m_Clip.Events[event].Name == name)
		{
			return true;
		}
		AnimationClipData edited = m_Clip;
		edited.Events[event].Name = name;
		commit(std::move(edited));
		return true;
	}

	bool ClipEditModel::remove_event(size_t event)
	{
		m_Error.clear();
		if (event >= m_Clip.Events.size())
		{
			return refuse("there is no such event");
		}
		AnimationClipData edited = m_Clip;
		edited.Events.erase(edited.Events.begin() + static_cast<std::ptrdiff_t>(event));
		commit(std::move(edited));
		return true;
	}
}
