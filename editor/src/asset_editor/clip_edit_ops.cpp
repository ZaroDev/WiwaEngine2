#include <editor_common.h>
#include "clip_edit_model.h"

#include <algorithm>
#include <cmath>

#include "clip_math.h"

// The whole-clip operations of ClipEditModel (trim, scale time, reverse, resample, reduce keys, make additive, rename).
namespace wiwa
{
	namespace
	{
		constexpr float TIME_EPSILON = 1e-5f;

		bool is_key_channel(const AnimationChannel& channel) { return channel.Path != AnimationPath::Weights && channel_valid(channel); }

		struct KeyData
		{
			float Time = 0.0f;
			glm::vec4 Value{ 0.0f };
			glm::vec4 In{ 0.0f };
			glm::vec4 Out{ 0.0f };
		};

		std::vector<KeyData> read_keys(const AnimationChannel& channel)
		{
			std::vector<KeyData> keys;
			for (size_t k = 0; k < channel.Times.size(); k++)
			{
				keys.push_back(KeyData{ channel.Times[k], key_value(channel, k), key_in_tangent(channel, k), key_out_tangent(channel, k) });
			}
			return keys;
		}

		// Puts the keys in the channel (times sorted already; the channel keeps its interpolation and components).
		void write_keys(AnimationChannel& channel, const std::vector<KeyData>& keys)
		{
			channel.Times.clear();
			channel.Values.assign(keys.size() * channel_stride(channel), 0.0f);
			for (size_t k = 0; k < keys.size(); k++)
			{
				channel.Times.push_back(keys[k].Time);
				wiwa::set_key_value(channel, k, keys[k].Value);
				set_key_tangents(channel, k, keys[k].In, keys[k].Out);
			}
		}

		glm::quat as_quat(const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); }
		glm::vec4 as_vec(const glm::quat& q) { return glm::vec4(q.x, q.y, q.z, q.w); }

		// How far two values of a channel are: units for translation and scale, degrees for rotation.
		float key_error(const AnimationChannel& channel, const glm::vec4& a, const glm::vec4& b)
		{
			if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
			{
				const float d = std::min(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))), 1.0f);
				return glm::degrees(2.0f * std::acos(d));
			}
			return glm::length(a - b);
		}

		glm::vec4 interpolate(const AnimationChannel& channel, const KeyData& a, const KeyData& b, float time)
		{
			const float span = b.Time - a.Time;
			const float s = span > 0.0f ? (time - a.Time) / span : 0.0f;
			if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
			{
				return as_vec(glm::slerp(as_quat(a.Value), as_quat(b.Value), s));
			}
			return glm::mix(a.Value, b.Value, s);
		}
	}

	bool ClipEditModel::rename(const std::string& name)
	{
		m_Error.clear();
		if (name.empty() || name.size() > 128 || name.front() == ' ' || name.back() == ' ')
		{
			return refuse("a clip name is not empty, has no space at either end and is at most 128 characters");
		}
		if (m_Clip.Name == name)
		{
			return true;
		}
		AnimationClipData edited = m_Clip;
		edited.Name = name;
		commit(std::move(edited));
		return true;
	}

	bool ClipEditModel::trim(float from, float to)
	{
		m_Error.clear();
		if (!std::isfinite(from) || !std::isfinite(to) || from < 0.0f || to > m_Clip.Duration || !(from < to))
		{
			return refuse("the range has to be inside the clip and end after it starts");
		}
		if (from == 0.0f && to == m_Clip.Duration)
		{
			return true;
		}

		AnimationClipData edited = m_Clip;
		edited.Duration = to - from;
		for (AnimationChannel& channel : edited.Channels)
		{
			if (!channel_valid(channel))
			{
				continue;
			}
			const size_t count = channel.Times.size();
			if (count == 1)
			{
				channel.Times[0] = 0.0f; // a constant stays constant
				continue;
			}
			const bool keyed = is_key_channel(channel);
			const std::vector<KeyData> keys = read_keys(channel);
			std::vector<KeyData> kept;
			bool hasStart = false;
			bool hasEnd = false;
			for (const KeyData& key : keys)
			{
				if (key.Time < from - TIME_EPSILON || key.Time > to + TIME_EPSILON)
				{
					continue;
				}
				KeyData moved = key;
				moved.Time = std::clamp(key.Time - from, 0.0f, edited.Duration);
				hasStart = hasStart || moved.Time <= TIME_EPSILON;
				hasEnd = hasEnd || moved.Time >= edited.Duration - TIME_EPSILON;
				kept.push_back(moved);
			}
			// the clip looks the same inside the range: keys where it starts and ends
			const auto boundary = [&](float time, float at)
			{
				KeyData key;
				key.Time = at;
				if (keyed)
				{
					key.Value = sample_channel(channel, time);
					if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
					{
						key.Value = glm::normalize(key.Value);
					}
				}
				else
				{
					// weights: the nearest key at or before the time
					size_t nearest = 0;
					for (size_t k = 0; k < keys.size(); k++)
					{
						if (keys[k].Time <= time)
						{
							nearest = k;
						}
					}
					key.Value = keys[nearest].Value;
				}
				return key;
			};
			if (!hasStart)
			{
				kept.insert(kept.begin(), boundary(from, 0.0f));
			}
			if (!hasEnd)
			{
				kept.push_back(boundary(to, edited.Duration));
			}
			write_keys(channel, kept);
		}

		std::vector<AnimationEvent> events;
		for (const AnimationEvent& event : edited.Events)
		{
			if (event.Time >= from && event.Time <= to)
			{
				events.push_back(AnimationEvent{ event.Time - from, event.Name });
			}
		}
		edited.Events = std::move(events);
		commit(std::move(edited));
		return true;
	}

	bool ClipEditModel::scale_time(float factor)
	{
		m_Error.clear();
		if (!std::isfinite(factor) || factor <= 0.0f || m_Clip.Duration * factor > 1.0e6f)
		{
			return refuse("the factor has to be a number above zero");
		}
		if (factor == 1.0f)
		{
			return true;
		}
		AnimationClipData edited = m_Clip;
		edited.Duration *= factor;
		for (AnimationChannel& channel : edited.Channels)
		{
			const bool cubic = channel.Interpolation == AnimationInterpolation::CubicSpline && channel_valid(channel);
			for (float& time : channel.Times)
			{
				time *= factor;
			}
			if (cubic)
			{
				// a tangent is a change per second: it shrinks as the clip stretches
				const size_t stride = channel_stride(channel);
				for (size_t k = 0; k < channel.Times.size(); k++)
				{
					for (u32 c = 0; c < channel.Components; c++)
					{
						channel.Values[k * stride + c] /= factor;
						channel.Values[k * stride + 2 * channel.Components + c] /= factor;
					}
				}
			}
		}
		for (AnimationEvent& event : edited.Events)
		{
			event.Time *= factor;
		}
		commit(std::move(edited));
		return true;
	}

	bool ClipEditModel::set_duration(float seconds)
	{
		m_Error.clear();
		if (!std::isfinite(seconds) || seconds <= 0.0f || !(m_Clip.Duration > 0.0f))
		{
			return refuse("the duration has to be above zero, and the clip needs a duration to scale");
		}
		return scale_time(seconds / m_Clip.Duration);
	}

	bool ClipEditModel::reverse()
	{
		m_Error.clear();
		const float duration = m_Clip.Duration;
		AnimationClipData edited = m_Clip;
		for (AnimationChannel& channel : edited.Channels)
		{
			if (!channel_valid(channel))
			{
				continue;
			}
			std::vector<KeyData> keys = read_keys(channel);
			std::vector<KeyData> flipped;
			for (auto it = keys.rbegin(); it != keys.rend(); ++it)
			{
				KeyData key = *it;
				key.Time = std::clamp(duration - it->Time, 0.0f, duration);
				key.In = -it->Out; // time runs the other way: the slopes change sign and swap
				key.Out = -it->In;
				flipped.push_back(key);
			}
			write_keys(channel, flipped);
		}
		std::reverse(edited.Events.begin(), edited.Events.end());
		for (AnimationEvent& event : edited.Events)
		{
			event.Time = std::clamp(duration - event.Time, 0.0f, duration);
		}
		commit(std::move(edited));
		return true;
	}

	bool ClipEditModel::resample(float fps)
	{
		m_Error.clear();
		if (!std::isfinite(fps) || fps < 1.0f || fps > 240.0f)
		{
			return refuse("the frame rate has to be between 1 and 240");
		}
		if (!(m_Clip.Duration > 0.0f))
		{
			return refuse("a clip with no duration has nothing to resample");
		}
		AnimationClipData edited = m_Clip;
		std::vector<float> times;
		for (int i = 0;; i++)
		{
			const float t = static_cast<float>(i) / fps;
			if (t >= edited.Duration - TIME_EPSILON)
			{
				break;
			}
			times.push_back(t);
		}
		times.push_back(edited.Duration);

		for (AnimationChannel& channel : edited.Channels)
		{
			if (!is_key_channel(channel))
			{
				continue; // weights and damaged channels stay as they are
			}
			std::vector<KeyData> keys;
			glm::quat previous(1.0f, 0.0f, 0.0f, 0.0f);
			for (const float t : times)
			{
				KeyData key;
				key.Time = t;
				key.Value = sample_channel(channel, t);
				if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
				{
					glm::quat q = glm::normalize(as_quat(key.Value));
					q = keys.empty() ? q : quat_continuous(q, previous);
					previous = q;
					key.Value = as_vec(q);
				}
				keys.push_back(key);
			}
			channel.Interpolation = AnimationInterpolation::Linear;
			write_keys(channel, keys);
		}
		commit(std::move(edited));
		return true;
	}

	size_t ClipEditModel::reduce_keys(float translationTolerance, float rotationToleranceDegrees, float scaleTolerance)
	{
		m_Error.clear();
		if (!(translationTolerance >= 0.0f) || !(rotationToleranceDegrees >= 0.0f) || !(scaleTolerance >= 0.0f)
			|| !std::isfinite(translationTolerance + rotationToleranceDegrees + scaleTolerance))
		{
			refuse("a tolerance is a number that is not below zero");
			return 0;
		}
		AnimationClipData edited = m_Clip;
		size_t dropped = 0;
		for (AnimationChannel& channel : edited.Channels)
		{
			if (!is_key_channel(channel) || channel.Interpolation != AnimationInterpolation::Linear || channel.Times.size() < 3)
			{
				continue;
			}
			const float tolerance = channel.Path == AnimationPath::Translation ? translationTolerance
				: (channel.Path == AnimationPath::Rotation ? rotationToleranceDegrees : scaleTolerance);
			const std::vector<KeyData> original = read_keys(channel);
			std::vector<size_t> active(original.size());
			for (size_t i = 0; i < active.size(); i++)
			{
				active[i] = i;
			}
			std::vector<bool> blocked(original.size(), false);

			while (true)
			{
				// the key whose removal changes the curve the least
				size_t best = SIZE_MAX;
				float bestError = 1e30f;
				for (size_t a = 1; a + 1 < active.size(); a++)
				{
					if (blocked[active[a]])
					{
						continue;
					}
					const KeyData& prev = original[active[a - 1]];
					const KeyData& next = original[active[a + 1]];
					const float error = key_error(channel, original[active[a]].Value, interpolate(channel, prev, next, original[active[a]].Time));
					if (error <= tolerance && error < bestError)
					{
						bestError = error;
						best = a;
					}
				}
				if (best == SIZE_MAX)
				{
					break;
				}
				// every original key under the new segment has to stay within the tolerance too
				const KeyData& prev = original[active[best - 1]];
				const KeyData& next = original[active[best + 1]];
				bool ok = true;
				for (size_t k = active[best - 1] + 1; k < active[best + 1] && ok; k++)
				{
					ok = key_error(channel, original[k].Value, interpolate(channel, prev, next, original[k].Time)) <= tolerance;
				}
				if (ok)
				{
					active.erase(active.begin() + static_cast<std::ptrdiff_t>(best));
					dropped++;
				}
				else
				{
					blocked[active[best]] = true;
				}
			}
			if (active.size() != original.size())
			{
				std::vector<KeyData> kept;
				for (const size_t k : active)
				{
					kept.push_back(original[k]);
				}
				write_keys(channel, kept);
			}
		}
		if (dropped > 0)
		{
			commit(std::move(edited));
		}
		return dropped;
	}

	bool ClipEditModel::make_additive(float referenceTime)
	{
		m_Error.clear();
		if (!std::isfinite(referenceTime) || referenceTime < 0.0f || referenceTime > m_Clip.Duration)
		{
			return refuse("the reference time is outside the clip");
		}
		AnimationClipData edited = m_Clip;
		bool changed = false;
		for (AnimationChannel& channel : edited.Channels)
		{
			if (!is_key_channel(channel))
			{
				continue;
			}
			// a delta of a curve is made on its keys: the tangents are dropped (the channel becomes linear)
			if (channel.Interpolation == AnimationInterpolation::CubicSpline)
			{
				set_channel_interpolation(channel, AnimationInterpolation::Linear);
			}
			const glm::vec4 reference = sample_channel(channel, referenceTime);
			for (size_t k = 0; k < channel.Times.size(); k++)
			{
				const glm::vec4 value = key_value(channel, k);
				glm::vec4 delta = value;
				if (channel.Path == AnimationPath::Translation)
				{
					delta = value - reference;
				}
				else if (channel.Path == AnimationPath::Scale)
				{
					for (int c = 0; c < 3; c++)
					{
						delta[c] = std::abs(reference[c]) > 1e-6f ? value[c] / reference[c] : 1.0f;
					}
				}
				else if (channel.Components == 4)
				{
					glm::quat q = glm::normalize(as_quat(value)) * glm::inverse(glm::normalize(as_quat(reference)));
					delta = as_vec(glm::normalize(q));
				}
				wiwa::set_key_value(channel, k, delta);
				changed = true;
			}
			// the rotation deltas take the short way from key to key
			if (channel.Path == AnimationPath::Rotation && channel.Components == 4)
			{
				for (size_t k = 1; k < channel.Times.size(); k++)
				{
					const glm::quat q = quat_continuous(as_quat(key_value(channel, k)), as_quat(key_value(channel, k - 1)));
					wiwa::set_key_value(channel, k, as_vec(q));
				}
			}
		}
		if (changed)
		{
			commit(std::move(edited));
		}
		return true;
	}
}
