#pragma once

#include <memory>
#include <string>
#include <vector>

#include "clip_binding.h"
#include "pose.h"

namespace wiwa
{
	// A clip event that fired while a node advanced.
	struct ClipEvent
	{
		std::string Name{};
		std::string Clip{};
		float Time = 0.0f; // on the clip's own time
	};

	// A node of the small tree an animator evaluates. advance() moves time and state and may emit events; it touches only the node and its
	// children. evaluate() writes a pose and depends only on the state of the node and its children (a pose that arrives sized to the
	// skeleton, holding the rest pose, is the input).
	class PoseNode
	{
	public:
		virtual ~PoseNode() = default;

		// weight is how much of the final pose this node is: a clip fires its events only when it is above 0.5.
		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) = 0;
		virtual void evaluate(Pose& out) const = 0;
		// This node and everything below it.
		virtual u32 node_count() const { return 1; }
		// The node whose weight is 1 for the pose after the nodes below it faded out, so the tree can be shortened. Null: keep.
		virtual std::unique_ptr<PoseNode> collapse() { return nullptr; }
	};

	// Plays one clip on one skeleton. The clip, the skeleton and the binding must outlive the node (the animator keeps their handles).
	class ClipNode final : public PoseNode
	{
	public:
		ClipNode(const AnimationClipData& clip, const SkeletonData& skeleton, std::shared_ptr<const ClipBinding> binding);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;

		// The pose at a time of the clip, over what is in out.
		void sample(float time, Pose& out) const;

		float time() const { return m_Time; }
		void set_time(float time);
		void set_loop(bool loop) { m_Loop = loop; }
		bool loop() const { return m_Loop; }
		void set_speed(float speed) { m_Speed = std::max(speed, 0.0f); }
		float speed() const { return m_Speed; }
		// A clip that plays once and has reached its end.
		bool finished() const { return m_Finished; }
		const AnimationClipData& clip() const { return *m_Clip; }

	private:
		const AnimationClipData* m_Clip;
		const SkeletonData* m_Skeleton;
		std::shared_ptr<const ClipBinding> m_Binding;
		float m_Time = 0.0f;
		float m_Speed = 1.0f;
		bool m_Loop = true;
		bool m_Finished = false;
		bool m_Started = false;                 // the first advance includes the events at time 0
		mutable std::vector<u32> m_Cursors{};   // per channel: the key the last lookup found. Only a cache, it never changes a result
	};

	// a toward b by weight.
	class BlendNode final : public PoseNode
	{
	public:
		BlendNode(std::unique_ptr<PoseNode> a, std::unique_ptr<PoseNode> b, float weight);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override;
		virtual std::unique_ptr<PoseNode> collapse() override;

		void set_weight(float weight) { m_Weight = std::clamp(weight, 0.0f, 1.0f); }
		float weight() const { return m_Weight; }

	private:
		std::unique_ptr<PoseNode> m_A;
		std::unique_ptr<PoseNode> m_B;
		float m_Weight;
	};

	// base plus the change of a clip from its first frame, by weight (and per joint by the mask, if there is one).
	class AdditiveNode final : public PoseNode
	{
	public:
		AdditiveNode(std::unique_ptr<PoseNode> base, std::unique_ptr<ClipNode> additive, float weight, std::vector<float> mask = {});

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override;
		virtual std::unique_ptr<PoseNode> collapse() override;

		void set_weight(float weight) { m_Weight = std::clamp(weight, 0.0f, 1.0f); }
		float weight() const { return m_Weight; }
		ClipNode& additive() { return *m_Additive; }

	private:
		std::unique_ptr<PoseNode> m_Base;
		std::unique_ptr<ClipNode> m_Additive;
		float m_Weight;
		std::vector<float> m_Mask;
	};

	// base with layer over it, joint by joint by jointWeights times weight: a layer limited to part of the body.
	class MaskNode final : public PoseNode
	{
	public:
		MaskNode(std::unique_ptr<PoseNode> base, std::unique_ptr<PoseNode> layer, std::vector<float> jointWeights, float weight);

		virtual void advance(float deltaTime, float weight, std::vector<ClipEvent>& events) override;
		virtual void evaluate(Pose& out) const override;
		virtual u32 node_count() const override;
		virtual std::unique_ptr<PoseNode> collapse() override;

		void set_weight(float weight) { m_Weight = std::clamp(weight, 0.0f, 1.0f); }
		float weight() const { return m_Weight; }

	private:
		std::unique_ptr<PoseNode> m_Base;
		std::unique_ptr<PoseNode> m_Layer;
		std::vector<float> m_JointWeights;
		float m_Weight;
	};
}
