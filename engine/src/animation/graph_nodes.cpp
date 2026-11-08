#include <pch.h>
#include "graph_nodes.h"

#include <cmath>

namespace wiwa
{
	namespace
	{
		constexpr float MIN_WEIGHT = 1e-6f;
		constexpr float MIN_AREA = 1e-9f;

		// 1D: the two samples around x (x clamped to the range), the first sample at an x that several share.
		void weights_1d(const std::vector<glm::vec2>& points, const std::vector<size_t>& order, float x, std::vector<float>& weights)
		{
			if (order.size() == 1)
			{
				weights[order[0]] = 1.0f;
				return;
			}
			x = std::clamp(x, points[order.front()].x, points[order.back()].x);
			size_t j = 0;
			while (j < order.size() && points[order[j]].x < x)
			{
				j++;
			}
			if (j >= order.size() || points[order[j]].x == x || j == 0)
			{
				weights[order[std::min(j, order.size() - 1)]] = 1.0f;
				return;
			}
			const size_t lo = order[j - 1];
			const size_t hi = order[j];
			const float span = points[hi].x - points[lo].x;
			const float t = span > 0.0f ? (x - points[lo].x) / span : 0.0f;
			weights[lo] = 1.0f - t;
			weights[hi] = t;
		}

		// 2D: gradient bands (Johansen): a sample weighs the least, over the other samples, of 1 - (how far the point has moved from this sample toward that one,
		// as a share of the distance between them), never below 0; the weights are then made to sum to 1. It needs no triangles, so nothing jumps when the point
		// crosses where triangles would meet, and it is exact on a sample, handles samples on a line and points outside the samples. A sample that sits where an
		// earlier one does gets 0.
		void weights_2d(const std::vector<glm::vec2>& points, const glm::vec2& p, std::vector<float>& weights)
		{
			const size_t n = points.size();
			float sum = 0.0f;
			for (size_t i = 0; i < n; i++)
			{
				float w = 1.0f;
				for (size_t j = 0; j < n && w > 0.0f; j++)
				{
					if (j == i)
					{
						continue;
					}
					const glm::vec2 toOther = points[j] - points[i];
					const float length2 = glm::dot(toOther, toOther);
					if (length2 < 1e-12f)
					{
						if (j < i)
						{
							w = 0.0f; // the same place as an earlier sample
						}
						continue;
					}
					w = std::min(w, std::max(0.0f, 1.0f - glm::dot(p - points[i], toOther) / length2));
				}
				weights[i] = w;
				sum += w;
			}
			if (sum <= 1e-6f)
			{
				// cannot happen for distinct samples; if it does, the nearest sample
				size_t nearest = 0;
				for (size_t i = 1; i < n; i++)
				{
					if (glm::distance(points[i], p) < glm::distance(points[nearest], p))
					{
						nearest = i;
					}
				}
				std::fill(weights.begin(), weights.end(), 0.0f);
				weights[nearest] = 1.0f;
				return;
			}
			for (float& w : weights)
			{
				w /= sum;
			}
		}
	}

	ClipBody::ClipBody(std::unique_ptr<ClipNode> clip, bool loop) : m_Clip(std::move(clip))
	{
		m_Clip->set_loop(loop);
	}

	void ClipBody::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		if (m_Clip->finished())
		{
			m_Clip->set_time(m_Clip->clip().Duration); // ClipNode would wrap a finished clip back to its start; a state holds its last pose
			return;
		}
		m_Clip->advance(deltaTime, weight, events);
	}

	void ClipBody::evaluate(Pose& out) const
	{
		m_Clip->evaluate(out);
	}

	float ClipBody::normalized_time() const
	{
		const float duration = m_Clip->clip().Duration;
		return duration > 0.0f ? std::clamp(m_Clip->time() / duration, 0.0f, 1.0f) : 1.0f;
	}

	void ClipBody::restart()
	{
		m_Clip->set_time(0.0f);
	}

	BlendSpaceNode::BlendSpaceNode(std::vector<std::unique_ptr<ClipNode>> samples, std::vector<glm::vec2> points, bool loop, const GraphParams& params, int xParam, int yParam)
		: m_Samples(std::move(samples)), m_Points(std::move(points)), m_Loop(loop), m_Params(&params), m_XParam(xParam), m_YParam(yParam)
	{
		for (auto& sample : m_Samples)
		{
			sample->set_loop(loop);
		}
		m_Order.resize(m_Samples.size());
		for (size_t i = 0; i < m_Order.size(); i++)
		{
			m_Order[i] = i;
		}
		std::stable_sort(m_Order.begin(), m_Order.end(), [&](size_t a, size_t b) { return m_Points[a].x < m_Points[b].x; });
	}

	float BlendSpaceNode::parameter(int index) const
	{
		return index >= 0 ? m_Params->get(index) : 0.0f;
	}

	std::vector<float> BlendSpaceNode::sample_weights() const
	{
		std::vector<float> weights(m_Samples.size(), 0.0f);
		if (weights.empty())
		{
			return weights;
		}
		if (m_YParam < 0)
		{
			weights_1d(m_Points, m_Order, parameter(m_XParam), weights);
		}
		else
		{
			weights_2d(m_Points, glm::vec2(parameter(m_XParam), parameter(m_YParam)), weights);
		}
		return weights;
	}

	void BlendSpaceNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		const std::vector<float> weights = sample_weights();
		float blendedDuration = 0.0f;
		for (size_t i = 0; i < m_Samples.size(); i++)
		{
			blendedDuration += weights[i] * m_Samples[i]->clip().Duration;
		}
		if (blendedDuration < 1e-4f)
		{
			return;
		}
		const float step = std::max(deltaTime, 0.0f) * m_Speed / blendedDuration; // the part of a loop this advance covers
		for (size_t i = 0; i < m_Samples.size(); i++)
		{
			ClipNode& sample = *m_Samples[i];
			if (sample.finished())
			{
				sample.set_time(sample.clip().Duration);
				continue;
			}
			sample.advance(step * sample.clip().Duration, weight * weights[i], events);
		}
		m_Phase += step;
		if (m_Loop)
		{
			m_Phase -= std::floor(m_Phase);
		}
		else
		{
			m_Phase = std::min(m_Phase, 1.0f);
		}
	}

	void BlendSpaceNode::restart()
	{
		for (auto& sample : m_Samples)
		{
			sample->set_time(0.0f);
		}
		m_Phase = 0.0f;
	}

	void BlendSpaceNode::blend(Pose& out, bool atStart) const
	{
		const std::vector<float> weights = sample_weights();
		Pose accumulated;
		float total = 0.0f;
		for (size_t i = 0; i < m_Samples.size(); i++)
		{
			if (weights[i] <= MIN_WEIGHT)
			{
				continue;
			}
			Pose pose = out;
			if (atStart)
			{
				m_Samples[i]->sample(0.0f, pose);
			}
			else
			{
				m_Samples[i]->evaluate(pose);
			}
			if (total == 0.0f)
			{
				accumulated = std::move(pose);
				total = weights[i];
			}
			else
			{
				total += weights[i];
				blend_poses(accumulated, pose, weights[i] / total, nullptr, accumulated);
			}
		}
		if (total > 0.0f)
		{
			out = std::move(accumulated);
		}
	}

	void BlendSpaceNode::evaluate(Pose& out) const
	{
		blend(out, false);
	}

	void BlendSpaceNode::reference(Pose& out) const
	{
		blend(out, true);
	}

	StateMachineNode::StateMachineNode(StateMachineSetup setup, GraphParams& params) : m_Setup(std::move(setup)), m_Params(&params)
	{
		m_Active = std::clamp(m_Setup.Default, 0, static_cast<int>(m_Setup.States.size()) - 1);
	}

	bool StateMachineNode::conditions_hold(const StateMachineSetup::Transition& transition) const
	{
		if (transition.ExitTime >= 0.0f && normalized_time() < transition.ExitTime)
		{
			return false;
		}
		for (const StateMachineSetup::Condition& condition : transition.Conditions)
		{
			if (condition.Param < 0)
			{
				return false;
			}
			float value = m_Params->get(condition.Param);
			const bool isInt = m_Params->type_of(condition.Param) == GraphParamType::Int;
			if (isInt)
			{
				value = std::trunc(value);
			}
			bool holds = false;
			switch (condition.Op)
			{
			case GraphCompareOp::Greater: holds = value > condition.Value; break;
			case GraphCompareOp::Less: holds = value < condition.Value; break;
			case GraphCompareOp::GreaterEqual: holds = value >= condition.Value; break;
			case GraphCompareOp::LessEqual: holds = value <= condition.Value; break;
			case GraphCompareOp::Equal: holds = value == (isInt ? std::trunc(condition.Value) : condition.Value); break;
			case GraphCompareOp::NotEqual: holds = value != (isInt ? std::trunc(condition.Value) : condition.Value); break;
			case GraphCompareOp::IsTrue: holds = value != 0.0f; break;
			case GraphCompareOp::IsFalse: holds = value == 0.0f; break;
			case GraphCompareOp::Fired: holds = m_Params->fired(condition.Param); break;
			}
			if (!holds)
			{
				return false;
			}
		}
		return true;
	}

	void StateMachineNode::fire(const StateMachineSetup::Transition& transition)
	{
		for (const StateMachineSetup::Condition& condition : transition.Conditions)
		{
			if (condition.Op == GraphCompareOp::Fired)
			{
				m_Params->consume(condition.Param);
			}
		}

		if (m_Outgoing != NONE)
		{
			// a fade is running: hold the pose as it is and fade from that, so nothing jumps
			Pose pose = m_Setup.Rest;
			Pose reference = m_Setup.Rest;
			blended(pose, &reference);
			m_Frozen = std::move(pose);
			m_FrozenReference = std::move(reference);
			m_Outgoing = FROZEN;
		}
		else
		{
			m_Outgoing = m_Active;
		}
		m_Active = transition.To;
		m_Setup.States[static_cast<size_t>(m_Active)].Body->restart();
		if (transition.Duration <= 0.0f)
		{
			m_Outgoing = NONE;
			m_Fade = 1.0f;
			m_Rate = 0.0f;
		}
		else
		{
			m_Fade = 0.0f;
			m_Rate = 1.0f / transition.Duration;
		}
	}

	void StateMachineNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		const float dt = std::max(deltaTime, 0.0f);
		const auto apply_speed = [&](int index)
		{
			StateMachineSetup::State& state = m_Setup.States[static_cast<size_t>(index)];
			state.Body->set_speed(state.Speed * (state.SpeedParam >= 0 ? std::max(m_Params->get(state.SpeedParam), 0.0f) : 1.0f));
		};
		apply_speed(m_Active);
		if (m_Outgoing >= 0)
		{
			apply_speed(m_Outgoing);
		}

		if (m_Outgoing != NONE)
		{
			m_Fade = std::min(1.0f, m_Fade + m_Rate * dt);
			if (m_Fade >= 1.0f)
			{
				m_Outgoing = NONE;
			}
		}

		const bool fadingNow = m_Outgoing != NONE;
		m_Setup.States[static_cast<size_t>(m_Active)].Body->advance(dt, weight * (fadingNow ? m_Fade : 1.0f), events);
		if (m_Outgoing >= 0)
		{
			m_Setup.States[static_cast<size_t>(m_Outgoing)].Body->advance(dt, weight * (1.0f - m_Fade), events);
		}

		// from the active state first, then from any state, each in file order
		for (const int source : { m_Active, -1 })
		{
			bool fired = false;
			for (const StateMachineSetup::Transition& transition : m_Setup.Transitions)
			{
				if (transition.From != source || transition.To == m_Active || !conditions_hold(transition))
				{
					continue;
				}
				fire(transition);
				fired = true;
				break;
			}
			if (fired)
			{
				break;
			}
		}
	}

	void StateMachineNode::blended(Pose& pose, Pose* reference) const
	{
		const StateMachineSetup::State& active = m_Setup.States[static_cast<size_t>(m_Active)];
		Pose activePose = pose;
		active.Body->evaluate(activePose);
		Pose activeReference;
		if (reference != nullptr)
		{
			activeReference = pose;
			active.Body->reference(activeReference);
		}
		if (m_Outgoing == NONE)
		{
			pose = std::move(activePose);
			if (reference != nullptr)
			{
				*reference = std::move(activeReference);
			}
			return;
		}

		Pose outPose;
		Pose outReference;
		if (m_Outgoing == FROZEN)
		{
			outPose = m_Frozen;
			outReference = m_FrozenReference;
		}
		else
		{
			const StateMachineSetup::State& outgoing = m_Setup.States[static_cast<size_t>(m_Outgoing)];
			outPose = pose;
			outgoing.Body->evaluate(outPose);
			if (reference != nullptr)
			{
				outReference = pose;
				outgoing.Body->reference(outReference);
			}
		}
		blend_poses(outPose, activePose, m_Fade, nullptr, pose);
		if (reference != nullptr)
		{
			blend_poses(outReference, activeReference, m_Fade, nullptr, *reference);
		}
	}

	void StateMachineNode::evaluate(Pose& out) const
	{
		blended(out, nullptr);
	}

	void StateMachineNode::evaluate_with_reference(Pose& pose, Pose& reference) const
	{
		blended(pose, &reference);
	}

	u32 StateMachineNode::node_count() const
	{
		u32 count = 1;
		for (const StateMachineSetup::State& state : m_Setup.States)
		{
			count += state.Body->node_count();
		}
		return count;
	}

	void StateMachineNode::settle()
	{
		m_Outgoing = NONE;
		m_Fade = 1.0f;
		m_Rate = 0.0f;
	}

	AdditiveMachineNode::AdditiveMachineNode(std::unique_ptr<PoseNode> base, std::unique_ptr<StateMachineNode> machine, float weight, std::vector<float> mask)
		: m_Base(std::move(base)), m_Machine(std::move(machine)), m_Weight(std::clamp(weight, 0.0f, 1.0f)), m_Mask(std::move(mask))
	{
	}

	void AdditiveMachineNode::advance(float deltaTime, float weight, std::vector<ClipEvent>& events)
	{
		m_Base->advance(deltaTime, weight, events);
		m_Machine->advance(deltaTime, weight * m_Weight, events);
	}

	void AdditiveMachineNode::evaluate(Pose& out) const
	{
		Pose base = out;
		m_Base->evaluate(base);
		if (m_Weight <= 0.0f)
		{
			out = std::move(base);
			return;
		}
		Pose current = out;
		Pose reference = out;
		m_Machine->evaluate_with_reference(current, reference);
		add_pose(base, current, reference, m_Weight, &m_Mask, out);
	}
}
