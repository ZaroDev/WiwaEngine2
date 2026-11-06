#include <pch.h>
#include "pose_nodes.h"

namespace wiwa
{
	namespace
	{
		constexpr float EVENT_WEIGHT_THRESHOLD = 0.5f; // an event fires only for a clip that is more than half of the pose
		constexpr int MAX_EVENT_WRAPS = 8;             // a hitch longer than this many loops of a clip does not fire the events of the loops in between

		// The key a time falls in: times[key] <= t < times[key + 1]. t is strictly inside the keys. The cursor is where the last lookup ended.
		u32 find_key(const std::vector<float>& times, float t, u32& cursor)
		{
			const u32 last = static_cast<u32>(times.size()) - 1;
			u32 key = cursor;
			if (key + 1 <= last && times[key] <= t && t < times[key + 1])
			{
				return key;
			}
			if (key + 2 <= last && times[key + 1] <= t && t < times[key + 2])
			{
				cursor = key + 1;
				return cursor;
			}
			const auto above = std::upper_bound(times.begin(), times.end(), t);
			key = static_cast<u32>(std::min<size_t>(static_cast<size_t>(above - times.begin()), times.size()) - 1);
			cursor = std::min(key, last - 1);
			return cursor;
		}

		glm::vec4 read_vec(const std::vector<float>& values, size_t index, u32 components)
		{
			glm::vec4 result(0.0f);
			for (u32 k = 0; k < components && k < 4; k++)
			{
				result[static_cast<int>(k)] = values[index + k];
			}
			return result;
		}

		// The value of a channel at a time, as x y z (w). False when the channel has no usable keys.
		bool sample_channel(const AnimationChannel& channel, float t, u32& cursor, glm::vec4& result)
		{
			const size_t count = channel.Times.size();
			const u32 components = channel.Components;
			const bool cubic = channel.Interpolation == AnimationInterpolation::CubicSpline;
			const size_t stride = static_cast<size_t>(components) * (cubic ? 3 : 1);
			if (count == 0 || components == 0 || components > 4 || channel.Values.size() < count * stride)
			{
				return false;
			}

			auto value_at = [&](size_t key) { return read_vec(channel.Values, key * stride + (cubic ? components : 0), components); };

			if (count == 1 || t <= channel.Times.front())
			{
				result = value_at(0);
				return true;
			}
			if (t >= channel.Times.back())
			{
				result = value_at(count - 1);
				return true;
			}

			const u32 key = find_key(channel.Times, t, cursor);
			const float t0 = channel.Times[key];
			const float t1 = channel.Times[key + 1];
			const float span = t1 - t0;
			const float s = span > 0.0f ? (t - t0) / span : 0.0f;
			const bool rotation = channel.Path == AnimationPath::Rotation && components == 4;

			switch (channel.Interpolation)
			{
			case AnimationInterpolation::Step:
				result = value_at(key);
				break;
			case AnimationInterpolation::Linear:
			{
				const glm::vec4 v0 = value_at(key);
				const glm::vec4 v1 = value_at(key + 1);
				if (rotation)
				{
					const glm::quat q = glm::slerp(glm::quat(v0.w, v0.x, v0.y, v0.z), glm::quat(v1.w, v1.x, v1.y, v1.z), s);
					result = glm::vec4(q.x, q.y, q.z, q.w);
				}
				else
				{
					result = glm::mix(v0, v1, s);
				}
				break;
			}
			case AnimationInterpolation::CubicSpline:
			{
				const glm::vec4 v0 = value_at(key);
				const glm::vec4 out0 = read_vec(channel.Values, key * stride + 2 * components, components);
				const glm::vec4 in1 = read_vec(channel.Values, (key + 1) * stride, components);
				const glm::vec4 v1 = value_at(key + 1);
				const float s2 = s * s;
				const float s3 = s2 * s;
				result = (2.0f * s3 - 3.0f * s2 + 1.0f) * v0 + (s3 - 2.0f * s2 + s) * span * out0 + (-2.0f * s3 + 3.0f * s2) * v1 + (s3 - s2) * span * in1;
				if (rotation)
				{
					const float length = glm::length(result);
					result = length > 1e-8f ? result / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
				}
				break;
			}
			}
			return true;
		}
	}

	ClipNode::ClipNode(const AnimationClipData& clip, const SkeletonData& skeleton, std::shared_ptr<const ClipBinding> binding)
		: m_Clip(&clip), m_Skeleton(&skeleton), m_Binding(std::move(binding))
	{
		m_Cursors.assign(clip.Channels.size(), 0);
	}

	void ClipNode::set_time(float time)
	{
		const float duration = m_Clip->Duration;
		m_Time = duration > 0.0f ? std::clamp(time, 0.0f, duration) : 0.0f;
		m_Finished = !m_Loop && duration > 0.0f && m_Time >= duration;
		m_Started = true; // a jump does not fire what it jumped over
	}

	void ClipNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		const float duration = m_Clip->Duration;
		bool includeStart = !m_Started;
		m_Started = true;
		if (duration <= 0.0f || m_Finished)
		{
			m_Time = 0.0f;
			return;
		}

		auto fire = [&](float from, float to, bool includeFrom)
		{
			if (weight <= EVENT_WEIGHT_THRESHOLD)
			{
				return;
			}
			for (const AnimationEvent& event : m_Clip->Events)
			{
				if ((includeFrom ? event.Time >= from : event.Time > from) && event.Time <= to)
				{
					events.push_back(ClipEvent{ event.Name, m_Clip->Name, event.Time });
				}
			}
		};

		float delta = std::max(deltaTime, 0.0f) * m_Speed;
		if (!m_Loop)
		{
			const float end = std::min(m_Time + delta, duration);
			fire(m_Time, end, includeStart);
			m_Time = end;
			m_Finished = end >= duration;
			return;
		}

		const float wraps = std::floor(delta / duration);
		if (wraps > static_cast<float>(MAX_EVENT_WRAPS))
		{
			delta -= (wraps - static_cast<float>(MAX_EVENT_WRAPS)) * duration;
		}

		float current = m_Time;
		float remaining = delta;
		for (;;)
		{
			const float segmentEnd = current + remaining;
			if (segmentEnd < duration)
			{
				fire(current, segmentEnd, includeStart);
				current = segmentEnd;
				break;
			}

			fire(current, duration, includeStart);
			remaining = segmentEnd - duration;
			current = 0.0f;
			includeStart = true; // the wrap starts the clip again: its first moment counts
			if (remaining <= 0.0f)
			{
				m_Started = false; // landed exactly on the end: the next advance fires time 0
				break;
			}
		}
		m_Time = current;
	}

	void ClipNode::evaluate(Pose& out) const
	{
		sample(m_Time, out);
	}

	void ClipNode::sample(float time, Pose& out) const
	{
		if (m_Cursors.size() != m_Clip->Channels.size())
		{
			m_Cursors.assign(m_Clip->Channels.size(), 0);
		}

		for (size_t c = 0; c < m_Clip->Channels.size() && c < m_Binding->JointOfChannel.size(); c++)
		{
			const i32 joint = m_Binding->JointOfChannel[c];
			if (joint < 0 || static_cast<size_t>(joint) >= out.size())
			{
				continue;
			}

			const AnimationChannel& channel = m_Clip->Channels[c];
			glm::vec4 value;
			if (!sample_channel(channel, time, m_Cursors[c], value))
			{
				continue;
			}

			const size_t j = static_cast<size_t>(joint);
			switch (channel.Path)
			{
			case AnimationPath::Translation: out.T[j] = glm::vec3(value); break;
			case AnimationPath::Scale: out.S[j] = glm::vec3(value); break;
			case AnimationPath::Rotation: out.R[j] = glm::normalize(glm::quat(value.w, value.x, value.y, value.z)); break;
			case AnimationPath::Weights: break;
			}
		}
	}

	BlendNode::BlendNode(std::unique_ptr<PoseNode> a, std::unique_ptr<PoseNode> b, float weight)
		: m_A(std::move(a)), m_B(std::move(b)), m_Weight(std::clamp(weight, 0.0f, 1.0f))
	{
	}

	void BlendNode::ramp(float seconds)
	{
		m_Weight = 0.0f;
		m_Rate = seconds > 0.0f ? 1.0f / seconds : 0.0f;
		if (seconds <= 0.0f)
		{
			m_Weight = 1.0f;
		}
	}

	void BlendNode::settle()
	{
		m_A->settle();
		m_B->settle();
		m_Weight = 1.0f;
		m_Rate = 0.0f;
	}

	void BlendNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		if (m_Rate > 0.0f)
		{
			m_Weight = std::min(1.0f, m_Weight + m_Rate * std::max(deltaTime, 0.0f));
		}
		m_A->advance(deltaTime, weight * (1.0f - m_Weight), events);
		m_B->advance(deltaTime, weight * m_Weight, events);
	}

	void BlendNode::evaluate(Pose& out) const
	{
		Pose a = out;
		m_A->evaluate(a);
		if (m_Weight <= 0.0f)
		{
			out = std::move(a);
			return;
		}
		Pose b = out;
		m_B->evaluate(b);
		blend_poses(a, b, m_Weight, nullptr, out);
	}

	u32 BlendNode::node_count() const
	{
		return 1 + m_A->node_count() + m_B->node_count();
	}

	std::unique_ptr<PoseNode> BlendNode::collapse()
	{
		if (std::unique_ptr<PoseNode> replacement = m_A->collapse())
		{
			m_A = std::move(replacement);
		}
		if (std::unique_ptr<PoseNode> replacement = m_B->collapse())
		{
			m_B = std::move(replacement);
		}
		if (m_Weight <= 0.0f && m_Rate <= 0.0f)
		{
			return std::move(m_A); // not while it ramps: a frame of no time must not drop the clip that is about to fade in
		}
		if (m_Weight >= 1.0f)
		{
			return std::move(m_B);
		}
		return nullptr;
	}

	AdditiveNode::AdditiveNode(std::unique_ptr<PoseNode> base, std::unique_ptr<ClipNode> additive, float weight, std::vector<float> mask)
		: m_Base(std::move(base)), m_Additive(std::move(additive)), m_Weight(std::clamp(weight, 0.0f, 1.0f)), m_Mask(std::move(mask))
	{
	}

	void AdditiveNode::settle()
	{
		m_Base->settle();
	}

	void AdditiveNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		m_Base->advance(deltaTime, weight, events);
		m_Additive->advance(deltaTime, weight * m_Weight, events);
	}

	void AdditiveNode::evaluate(Pose& out) const
	{
		Pose base = out;
		m_Base->evaluate(base);
		if (m_Weight <= 0.0f)
		{
			out = std::move(base);
			return;
		}

		Pose current = out;
		m_Additive->evaluate(current);
		Pose reference = out;
		m_Additive->sample(0.0f, reference);

		add_pose(base, current, reference, m_Weight, &m_Mask, out);
	}

	u32 AdditiveNode::node_count() const
	{
		return 1 + m_Base->node_count() + 1;
	}

	std::unique_ptr<PoseNode> AdditiveNode::collapse()
	{
		if (std::unique_ptr<PoseNode> replacement = m_Base->collapse())
		{
			m_Base = std::move(replacement);
		}
		return m_Weight <= 0.0f ? std::move(m_Base) : nullptr;
	}

	MaskNode::MaskNode(std::unique_ptr<PoseNode> base, std::unique_ptr<PoseNode> layer, std::vector<float> jointWeights, float weight)
		: m_Base(std::move(base)), m_Layer(std::move(layer)), m_JointWeights(std::move(jointWeights)), m_Weight(std::clamp(weight, 0.0f, 1.0f))
	{
	}

	void MaskNode::settle()
	{
		m_Base->settle();
		m_Layer->settle();
	}

	void MaskNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		m_Base->advance(deltaTime, weight, events);
		m_Layer->advance(deltaTime, weight * m_Weight, events);
	}

	void MaskNode::evaluate(Pose& out) const
	{
		Pose base = out;
		m_Base->evaluate(base);
		if (m_Weight <= 0.0f)
		{
			out = std::move(base);
			return;
		}
		Pose layer = out;
		m_Layer->evaluate(layer);
		blend_poses(base, layer, m_Weight, &m_JointWeights, out);
	}

	u32 MaskNode::node_count() const
	{
		return 1 + m_Base->node_count() + m_Layer->node_count();
	}

	std::unique_ptr<PoseNode> MaskNode::collapse()
	{
		if (std::unique_ptr<PoseNode> replacement = m_Base->collapse())
		{
			m_Base = std::move(replacement);
		}
		if (std::unique_ptr<PoseNode> replacement = m_Layer->collapse())
		{
			m_Layer = std::move(replacement);
		}
		return m_Weight <= 0.0f ? std::move(m_Base) : nullptr;
	}
}
