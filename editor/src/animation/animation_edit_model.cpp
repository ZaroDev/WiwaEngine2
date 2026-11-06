#include <editor_common.h>
#include "animation_edit_model.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		std::string trimmed(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t");
			if (first == std::string::npos)
			{
				return {};
			}
			return text.substr(first, text.find_last_not_of(" \t") - first + 1);
		}
	}

	AnimationEditModel::AnimationEditModel(std::vector<EditableClip> clips) : m_Clips(std::move(clips))
	{
		for (EditableClip& clip : m_Clips)
		{
			std::stable_sort(clip.Events.begin(), clip.Events.end(), [](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
		}
	}

	AnimationEditModel AnimationEditModel::from_settings(const std::vector<std::pair<std::string, float>>& clips, const nlohmann::json& settings)
	{
		std::vector<EditableClip> editable;
		for (const auto& [name, duration] : clips)
		{
			editable.push_back(EditableClip{ name, duration, {} });
		}
		AnimationEditModel model(std::move(editable));

		const auto list = settings.find("animationEvents");
		if (list != settings.end() && list->is_array())
		{
			for (const nlohmann::json& entry : *list)
			{
				if (!entry.is_object() || !entry.contains("clip") || !entry["clip"].is_string() || !entry.contains("time") || !entry["time"].is_number()
					|| !entry.contains("name") || !entry["name"].is_string())
				{
					continue;
				}
				const auto clip = std::find_if(model.m_Clips.begin(), model.m_Clips.end(), [&](const EditableClip& c) { return c.Name == entry["clip"].get<std::string>(); });
				const std::string name = trimmed(entry["name"].get<std::string>());
				const float time = entry["time"].get<float>();
				const size_t index = static_cast<size_t>(clip - model.m_Clips.begin());
				if (clip != model.m_Clips.end() && valid_name(name) && model.valid_time(index, time))
				{
					model.insert_sorted(index, AnimationEvent{ time, name });
				}
			}
		}
		return model;
	}

	bool AnimationEditModel::valid_name(const std::string& name)
	{
		return !name.empty() && name.size() <= MAX_NAME_LENGTH && name == trimmed(name);
	}

	bool AnimationEditModel::valid_time(size_t clip, float time) const
	{
		return clip < m_Clips.size() && std::isfinite(time) && time >= 0.0f && time <= m_Clips[clip].Duration;
	}

	size_t AnimationEditModel::insert_sorted(size_t clip, AnimationEvent event)
	{
		std::vector<AnimationEvent>& events = m_Clips[clip].Events;
		const auto place = std::upper_bound(events.begin(), events.end(), event, [](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
		return static_cast<size_t>(events.insert(place, std::move(event)) - events.begin());
	}

	void AnimationEditModel::commit(Snapshot before)
	{
		m_Undo.push_back(std::move(before));
		while (m_Undo.size() > MAX_UNDO_STEPS)
		{
			m_Undo.pop_front();
		}
		m_Redo.clear();
		m_Revision = m_NextRevision++;
	}

	void AnimationEditModel::restore(Snapshot state)
	{
		m_Clips = std::move(state.Clips);
		m_Revision = state.Revision;
	}

	size_t AnimationEditModel::add_event(size_t clip, float time, const std::string& name)
	{
		const std::string clean = trimmed(name);
		if (!valid_name(clean) || !valid_time(clip, time))
		{
			return SIZE_MAX;
		}
		Snapshot before = snapshot();
		const size_t index = insert_sorted(clip, AnimationEvent{ time, clean });
		commit(std::move(before));
		return index;
	}

	size_t AnimationEditModel::move_event(size_t clip, size_t event, float time)
	{
		if (clip >= m_Clips.size() || event >= m_Clips[clip].Events.size() || !valid_time(clip, time))
		{
			return SIZE_MAX;
		}
		if (m_Clips[clip].Events[event].Time == time)
		{
			return event;
		}
		Snapshot before = snapshot();
		AnimationEvent moved = m_Clips[clip].Events[event];
		moved.Time = time;
		m_Clips[clip].Events.erase(m_Clips[clip].Events.begin() + static_cast<std::ptrdiff_t>(event));
		const size_t index = insert_sorted(clip, std::move(moved));
		commit(std::move(before));
		return index;
	}

	bool AnimationEditModel::rename_event(size_t clip, size_t event, const std::string& name)
	{
		const std::string clean = trimmed(name);
		if (clip >= m_Clips.size() || event >= m_Clips[clip].Events.size() || !valid_name(clean))
		{
			return false;
		}
		if (m_Clips[clip].Events[event].Name == clean)
		{
			return true;
		}
		Snapshot before = snapshot();
		m_Clips[clip].Events[event].Name = clean;
		commit(std::move(before));
		return true;
	}

	bool AnimationEditModel::remove_event(size_t clip, size_t event)
	{
		if (clip >= m_Clips.size() || event >= m_Clips[clip].Events.size())
		{
			return false;
		}
		Snapshot before = snapshot();
		m_Clips[clip].Events.erase(m_Clips[clip].Events.begin() + static_cast<std::ptrdiff_t>(event));
		commit(std::move(before));
		return true;
	}

	void AnimationEditModel::undo()
	{
		if (m_Undo.empty())
		{
			return;
		}
		m_Redo.push_back(snapshot());
		restore(std::move(m_Undo.back()));
		m_Undo.pop_back();
	}

	void AnimationEditModel::redo()
	{
		if (m_Redo.empty())
		{
			return;
		}
		m_Undo.push_back(snapshot());
		restore(std::move(m_Redo.back()));
		m_Redo.pop_back();
	}

	void AnimationEditModel::write_to(nlohmann::json& settings) const
	{
		nlohmann::json list = nlohmann::json::array();
		for (const EditableClip& clip : m_Clips)
		{
			for (const AnimationEvent& event : clip.Events)
			{
				list.push_back({ { "clip", clip.Name }, { "time", event.Time }, { "name", event.Name } });
			}
		}

		if (list.empty())
		{
			settings.erase("animationEvents");
		}
		else
		{
			settings["animationEvents"] = std::move(list);
		}
	}
}
