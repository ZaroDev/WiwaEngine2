#include <editor_common.h>
#include "clip_math.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		bool is_cubic(const AnimationChannel& channel) { return channel.Interpolation == AnimationInterpolation::CubicSpline; }

		glm::vec4 read_vec(const AnimationChannel& channel, size_t index)
		{
			glm::vec4 result(0.0f);
			for (u32 k = 0; k < channel.Components && k < 4; k++)
			{
				result[static_cast<int>(k)] = index + k < channel.Values.size() ? channel.Values[index + k] : 0.0f;
			}
			return result;
		}

		void write_vec(AnimationChannel& channel, size_t index, const glm::vec4& value)
		{
			for (u32 k = 0; k < channel.Components && k < 4; k++)
			{
				if (index + k < channel.Values.size())
				{
					channel.Values[index + k] = value[static_cast<int>(k)];
				}
			}
		}
	}

	size_t channel_stride(const AnimationChannel& channel)
	{
		return static_cast<size_t>(channel.Components) * (is_cubic(channel) ? 3 : 1);
	}

	bool channel_valid(const AnimationChannel& channel)
	{
		return channel.Components >= 1 && channel.Components <= 4 && !channel.Times.empty() && channel.Values.size() >= channel.Times.size() * channel_stride(channel);
	}

	glm::vec4 key_value(const AnimationChannel& channel, size_t key)
	{
		return read_vec(channel, key * channel_stride(channel) + (is_cubic(channel) ? channel.Components : 0));
	}

	glm::vec4 key_in_tangent(const AnimationChannel& channel, size_t key)
	{
		return is_cubic(channel) ? read_vec(channel, key * channel_stride(channel)) : glm::vec4(0.0f);
	}

	glm::vec4 key_out_tangent(const AnimationChannel& channel, size_t key)
	{
		return is_cubic(channel) ? read_vec(channel, key * channel_stride(channel) + 2 * channel.Components) : glm::vec4(0.0f);
	}

	void set_key_value(AnimationChannel& channel, size_t key, const glm::vec4& value)
	{
		write_vec(channel, key * channel_stride(channel) + (is_cubic(channel) ? channel.Components : 0), value);
	}

	void set_key_tangents(AnimationChannel& channel, size_t key, const glm::vec4& in, const glm::vec4& out)
	{
		if (is_cubic(channel))
		{
			write_vec(channel, key * channel_stride(channel), in);
			write_vec(channel, key * channel_stride(channel) + 2 * channel.Components, out);
		}
	}

	size_t insert_key_at(AnimationChannel& channel, float time, const glm::vec4& value)
	{
		const auto after = std::lower_bound(channel.Times.begin(), channel.Times.end(), time);
		const size_t index = static_cast<size_t>(after - channel.Times.begin());
		const size_t stride = channel_stride(channel);
		if (after != channel.Times.end() && *after == time)
		{
			set_key_value(channel, index, value);
			return index;
		}
		channel.Times.insert(after, time);
		channel.Values.insert(channel.Values.begin() + static_cast<std::ptrdiff_t>(index * stride), stride, 0.0f);
		set_key_value(channel, index, value);
		return index;
	}

	void erase_key(AnimationChannel& channel, size_t key)
	{
		const size_t stride = channel_stride(channel);
		channel.Times.erase(channel.Times.begin() + static_cast<std::ptrdiff_t>(key));
		channel.Values.erase(channel.Values.begin() + static_cast<std::ptrdiff_t>(key * stride), channel.Values.begin() + static_cast<std::ptrdiff_t>((key + 1) * stride));
	}

	glm::vec4 sample_channel(const AnimationChannel& channel, float t)
	{
		if (!channel_valid(channel))
		{
			return glm::vec4(0.0f);
		}
		const size_t count = channel.Times.size();
		if (count == 1 || t <= channel.Times.front())
		{
			return key_value(channel, 0);
		}
		if (t >= channel.Times.back())
		{
			return key_value(channel, count - 1);
		}

		const auto above = std::upper_bound(channel.Times.begin(), channel.Times.end(), t);
		const size_t key = std::min(static_cast<size_t>(above - channel.Times.begin()), count) - 1;
		const float t0 = channel.Times[key];
		const float span = channel.Times[key + 1] - t0;
		const float s = span > 0.0f ? (t - t0) / span : 0.0f;
		const bool rotation = channel.Path == AnimationPath::Rotation && channel.Components == 4;

		switch (channel.Interpolation)
		{
		case AnimationInterpolation::Step:
			return key_value(channel, key);
		case AnimationInterpolation::Linear:
		{
			const glm::vec4 v0 = key_value(channel, key);
			const glm::vec4 v1 = key_value(channel, key + 1);
			if (rotation)
			{
				const glm::quat q = glm::slerp(glm::quat(v0.w, v0.x, v0.y, v0.z), glm::quat(v1.w, v1.x, v1.y, v1.z), s);
				return glm::vec4(q.x, q.y, q.z, q.w);
			}
			return glm::mix(v0, v1, s);
		}
		case AnimationInterpolation::CubicSpline:
		{
			const glm::vec4 v0 = key_value(channel, key);
			const glm::vec4 out0 = key_out_tangent(channel, key);
			const glm::vec4 in1 = key_in_tangent(channel, key + 1);
			const glm::vec4 v1 = key_value(channel, key + 1);
			const float s2 = s * s;
			const float s3 = s2 * s;
			glm::vec4 result = (2.0f * s3 - 3.0f * s2 + 1.0f) * v0 + (s3 - 2.0f * s2 + s) * span * out0 + (-2.0f * s3 + 3.0f * s2) * v1 + (s3 - s2) * span * in1;
			if (rotation)
			{
				const float length = glm::length(result);
				result = length > 1e-8f ? result / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			}
			return result;
		}
		}
		return glm::vec4(0.0f);
	}

	void set_channel_interpolation(AnimationChannel& channel, AnimationInterpolation interpolation)
	{
		if (channel.Interpolation == interpolation || !channel_valid(channel))
		{
			return;
		}
		const size_t count = channel.Times.size();
		std::vector<glm::vec4> values(count);
		for (size_t k = 0; k < count; k++)
		{
			values[k] = key_value(channel, k);
		}
		channel.Interpolation = interpolation;
		channel.Values.assign(count * channel_stride(channel), 0.0f);
		for (size_t k = 0; k < count; k++)
		{
			set_key_value(channel, k, values[k]);
		}
	}

	size_t channel_bytes(const AnimationChannel& channel)
	{
		return sizeof(AnimationChannel) + channel.Joint.size() + (channel.Times.size() + channel.Values.size()) * sizeof(float);
	}

	size_t clip_bytes(const AnimationClipData& clip)
	{
		size_t total = sizeof(AnimationClipData) + clip.Name.size();
		for (const AnimationChannel& channel : clip.Channels)
		{
			total += channel_bytes(channel);
		}
		for (const AnimationEvent& event : clip.Events)
		{
			total += sizeof(AnimationEvent) + event.Name.size();
		}
		return total;
	}

	glm::quat quat_continuous(const glm::quat& q, const glm::quat& previous)
	{
		return glm::dot(q, previous) < 0.0f ? -q : q;
	}

	glm::vec3 quat_to_euler_degrees(const glm::quat& q, const glm::vec3& reference)
	{
		const glm::vec3 base = glm::degrees(glm::eulerAngles(glm::normalize(q)));
		const glm::vec3 candidates[2] = { base, glm::vec3(base.x + 180.0f, 180.0f - base.y, base.z + 180.0f) };
		glm::vec3 best = base;
		float bestDistance = 1e30f;
		for (const glm::vec3& candidate : candidates)
		{
			glm::vec3 shifted = candidate;
			for (int axis = 0; axis < 3; axis++)
			{
				shifted[axis] += 360.0f * std::round((reference[axis] - shifted[axis]) / 360.0f);
			}
			const float distance = glm::dot(shifted - reference, shifted - reference);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = shifted;
			}
		}
		return best;
	}

	glm::quat euler_degrees_to_quat(const glm::vec3& degrees)
	{
		return glm::normalize(glm::quat(glm::radians(degrees)));
	}
}
