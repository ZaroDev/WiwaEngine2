#pragma once

#include <memory>
#include <string>
#include <vector>

#include "graph_params.h"
#include "pose_nodes.h"

namespace wiwa
{
	// What a state of a graph plays: a clip or a blend space. restart() starts it over (a state is entered at its first frame).
	class StateBody : public PoseNode
	{
	public:
		// 0..1 through one loop (1 once a clip that plays once has ended).
		virtual float normalized_time() const = 0;
		virtual void restart() = 0;
		// A multiplier of the playing speed (0 and up).
		virtual void set_speed(float speed) = 0;
		// The pose of the first frame, over what is in out (the reference of an additive layer).
		virtual void reference(Pose& out) const = 0;
	};

	// One clip as a state.
	class ClipBody final : public StateBody
	{
	public:
		ClipBody(std::unique_ptr<ClipNode> clip, bool loop);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override { return 2; }
		virtual float normalized_time() const override;
		virtual void restart() override;
		virtual void set_speed(float speed) override { m_Clip->set_speed(speed); }
		virtual void reference(Pose& out) const override { m_Clip->sample(0.0f, out); }

	private:
		std::unique_ptr<ClipNode> m_Clip;
	};

	// Clips blended by one or two float parameters. One shared phase (0..1 through a loop) drives every clip so that their steps stay together;
	// each clip fires its own events, and only the clip that is more than half of the pose does (the 0.5 rule of ClipNode).
	class BlendSpaceNode final : public StateBody
	{
	public:
		// points: the (x, y) of each sample. yParam < 0 makes it a 1D blend space. The params object must outlive the node.
		BlendSpaceNode(std::vector<std::unique_ptr<ClipNode>> samples, std::vector<glm::vec2> points, bool loop, const GraphParams& params, int xParam, int yParam);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override { return 1 + static_cast<u32>(m_Samples.size()); }
		virtual float normalized_time() const override { return m_Phase; }
		virtual void restart() override;
		virtual void set_speed(float speed) override { m_Speed = std::max(speed, 0.0f); }
		virtual void reference(Pose& out) const override;

		// The weight of each sample for the parameters as they are now (sums to 1).
		std::vector<float> sample_weights() const;

	private:
		void blend(Pose& out, bool atStart) const;
		float parameter(int index) const;

		std::vector<std::unique_ptr<ClipNode>> m_Samples;
		std::vector<glm::vec2> m_Points;
		std::vector<size_t> m_Order{}; // 1D: sample indices by x
		bool m_Loop;
		const GraphParams* m_Params;
		int m_XParam;
		int m_YParam;
		float m_Phase = 0.0f;
		float m_Speed = 1.0f;
	};

	struct StateMachineSetup
	{
		struct State
		{
			std::string Name{};
			std::unique_ptr<StateBody> Body{};
			float Speed = 1.0f;
			int SpeedParam = -1; // a float parameter that multiplies Speed
		};

		struct Condition
		{
			int Param = -1;
			GraphCompareOp Op = GraphCompareOp::Greater;
			float Value = 0.0f;
		};

		struct Transition
		{
			int From = -1; // -1: from any state
			int To = 0;
			std::vector<Condition> Conditions{};
			float Duration = 0.2f;
			float ExitTime = -1.0f;
		};

		std::vector<State> States{};
		std::vector<Transition> Transitions{};
		int Default = 0;
		Pose Rest{}; // the skeleton's rest pose: what a frozen pose is made over
	};

	// The states of one graph layer and the transitions between them. Each advance: the active state (and the one fading out) move, then the
	// transitions are looked at, those from the active state first and then those from any state, each in file order; the first whose conditions all
	// hold (and whose exit time has been reached) fires. A transition to the active state is ignored. A fire that comes while another fade runs
	// freezes the pose as it is and fades from that, so the pose never jumps. The triggers a fired transition used are consumed.
	class StateMachineNode final : public PoseNode
	{
	public:
		StateMachineNode(StateMachineSetup setup, GraphParams& params);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override;
		virtual void settle() override;

		// The pose and, for an additive layer, the pose of the first frame of each state blended the same way (so the layer adds a change, not a pose).
		void evaluate_with_reference(Pose& pose, Pose& reference) const;

		const std::string& active_state() const { return m_Setup.States[static_cast<size_t>(m_Active)].Name; }
		int active_index() const { return m_Active; }
		bool fading() const { return m_Outgoing != NONE; }
		// How much of the pose the active state is while a fade runs (1 when none does).
		float fade_weight() const { return m_Fade; }
		float normalized_time() const { return m_Setup.States[static_cast<size_t>(m_Active)].Body->normalized_time(); }

	private:
		static constexpr int NONE = -1;
		static constexpr int FROZEN = -2;

		bool conditions_hold(const StateMachineSetup::Transition& transition) const;
		void fire(const StateMachineSetup::Transition& transition);
		void blended(Pose& pose, Pose* reference) const;

		StateMachineSetup m_Setup;
		GraphParams* m_Params;
		int m_Active = 0;
		int m_Outgoing = NONE; // a state that keeps playing while it fades out, FROZEN (the held pose), or NONE
		float m_Fade = 1.0f;   // the weight of the active state
		float m_Rate = 0.0f;
		Pose m_Frozen{};
		Pose m_FrozenReference{};
	};

	// base plus the change of the machine's states from their first frame, by weight (and per joint by the mask): an additive graph layer.
	class AdditiveMachineNode final : public PoseNode
	{
	public:
		AdditiveMachineNode(std::unique_ptr<PoseNode> base, std::unique_ptr<StateMachineNode> machine, float weight, std::vector<float> mask);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override { return 1 + m_Base->node_count() + m_Machine->node_count(); }
		virtual void settle() override { m_Base->settle(); m_Machine->settle(); }

		void set_weight(float weight) { m_Weight = std::clamp(weight, 0.0f, 1.0f); }
		float weight() const { return m_Weight; }

	private:
		std::unique_ptr<PoseNode> m_Base;
		std::unique_ptr<StateMachineNode> m_Machine;
		float m_Weight;
		std::vector<float> m_Mask;
	};
}
