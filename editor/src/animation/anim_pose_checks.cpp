#include <editor_common.h>
#include "anim_check.h"

#include <animation/clip_binding.h>
#include <animation/pose.h>
#include <animation/pose_nodes.h>

namespace wiwa
{
	namespace
	{
		bool near_equal(float a, float b, float epsilon = 1e-4f)
		{
			return std::abs(a - b) < epsilon;
		}

		// hip -> mid -> tip, the prefab nodes 10, 11 and 12. Rest: hip at (0,1,0), mid 1 along x, tip 1 along z.
		SkeletonData make_skeleton()
		{
			SkeletonData skeleton;
			skeleton.Name = "chain";
			skeleton.JointNames = { "hip", "mid", "tip" };
			skeleton.Parents = { -1, 0, 1 };
			skeleton.RestTranslation = { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
			skeleton.RestRotation = { glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };
			skeleton.RestScale = { glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f) };

			const Pose rest = rest_pose(skeleton);
			std::vector<glm::mat4> model;
			model_matrices(skeleton, rest, model);
			for (const glm::mat4& matrix : model)
			{
				skeleton.InverseBind.push_back(glm::inverse(matrix));
			}
			return skeleton;
		}

		AnimationChannel make_channel(u32 node, AnimationPath path, AnimationInterpolation interpolation, std::vector<float> times, std::vector<float> values)
		{
			AnimationChannel channel;
			channel.Joint = node == 10 ? "hip" : (node == 11 ? "mid" : (node == 12 ? "tip" : "node" + std::to_string(node)));
			channel.Path = path;
			channel.Interpolation = interpolation;
			channel.Components = path == AnimationPath::Rotation ? 4 : 3;
			channel.Times = std::move(times);
			channel.Values = std::move(values);
			return channel;
		}

		// A translation channel that moves along x only.
		AnimationChannel x_channel(u32 node, AnimationInterpolation interpolation, std::vector<float> times, const std::vector<float>& xs)
		{
			std::vector<float> values;
			for (float x : xs)
			{
				values.insert(values.end(), { x, 0.0f, 0.0f });
			}
			return make_channel(node, AnimationPath::Translation, interpolation, std::move(times), std::move(values));
		}

		std::unique_ptr<ClipNode> make_clip_node(const AnimationClipData& clip, const SkeletonData& skeleton, bool loop = false)
		{
			auto node = std::make_unique<ClipNode>(clip, skeleton, std::make_shared<const ClipBinding>(bind_clip(clip, skeleton)));
			node->set_loop(loop);
			return node;
		}

		Pose sampled(const PoseNode& node, const SkeletonData& skeleton)
		{
			Pose pose = rest_pose(skeleton);
			node.evaluate(pose);
			return pose;
		}

		void rest_and_hierarchy(AnimCheck& check)
		{
			const SkeletonData skeleton = make_skeleton();
			std::vector<glm::mat4> model;
			model_matrices(skeleton, rest_pose(skeleton), model);
			check(model.size() == 3 && near_equal(model[2][3].x, 1.0f) && near_equal(model[2][3].y, 1.0f) && near_equal(model[2][3].z, 1.0f), "model matrices accumulate down the chain");

			std::vector<JointMatrix> palette;
			build_palette(skeleton, model, palette);
			bool identity = palette.size() == 3;
			for (size_t j = 0; identity && j < palette.size(); j++)
			{
				const float expected[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
				for (int row = 0; row < 3; row++)
				{
					for (int column = 0; column < 4; column++)
					{
						identity = identity && near_equal(palette[j].Row[row][column], expected[row * 4 + column]);
					}
				}
			}
			check(identity, "the palette of the rest pose is the identity");

			check(find_joint(skeleton, "mid") == 1 && find_joint(skeleton, "nope") == -1, "find_joint by name");
			const std::vector<float> mask = subtree_mask(skeleton, 1);
			check(mask == std::vector<float>({ 0.0f, 1.0f, 1.0f }), "a subtree mask holds the joint and its descendants");
		}

		// A rig exported with an Armature node above the root joint (scale 0.01, moved up): the node folds into the joint as Pre, and a clip that
		// animates the joint must not lose it.
		void folded_nodes(AnimCheck& check)
		{
			SkeletonData skeleton = make_skeleton();
			skeleton.Pre.assign(3, glm::mat4(1.0f));
			skeleton.Pre[0] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
			skeleton.RestTranslation[0] = glm::vec3(0.0f, 100.0f, 0.0f);

			std::vector<glm::mat4> model;
			model_matrices(skeleton, rest_pose(skeleton), model);
			check(near_equal(model[0][3].y, 6.0f) && near_equal(model[0][0][0], 0.01f), "the folded node is part of the joint's matrix");
			check(near_equal(model[1][3].y, 6.0f) && near_equal(model[1][3].x, 0.01f), "and of its children");

			AnimationClipData clip;
			clip.Name = "move";
			clip.Duration = 1.0f;
			clip.Channels.push_back(make_channel(10, AnimationPath::Translation, AnimationInterpolation::Step, { 0 }, { 0.0f, 200.0f, 0.0f }));
			auto node = make_clip_node(clip, skeleton);
			Pose pose = rest_pose(skeleton);
			node->evaluate(pose);
			model_matrices(skeleton, pose, model);
			check(near_equal(model[0][3].y, 7.0f) && near_equal(model[0][1][1], 0.01f), "an animated joint keeps the folded node above it (200 local units are 2 world units on top of the 5)");
		}

		void sampling(AnimCheck& check)
		{
			const SkeletonData skeleton = make_skeleton();

			AnimationClipData linear;
			linear.Name = "linear";
			linear.Duration = 2.0f;
			linear.Channels.push_back(x_channel(10, AnimationInterpolation::Linear, { 0, 1, 2 }, { 0, 10, 20 }));
			auto node = make_clip_node(linear, skeleton);
			std::vector<ClipEvent> events;
			node->advance(0.5f, 1.0f, events);
			check(near_equal(sampled(*node, skeleton).T[0].x, 5.0f), "linear sampling between two keys");
			node->advance(1.0f, 1.0f, events);
			check(near_equal(sampled(*node, skeleton).T[0].x, 15.0f), "linear sampling reads the next segment");
			node->advance(5.0f, 1.0f, events);
			check(near_equal(sampled(*node, skeleton).T[0].x, 20.0f) && node->finished(), "a clip that plays once stays on its last key");
			check(near_equal(sampled(*node, skeleton).T[1].x, 1.0f), "a joint without a channel keeps its rest pose");

			AnimationClipData step = linear;
			step.Channels[0].Interpolation = AnimationInterpolation::Step;
			auto stepNode = make_clip_node(step, skeleton);
			stepNode->advance(0.5f, 1.0f, events);
			check(near_equal(sampled(*stepNode, skeleton).T[0].x, 0.0f), "step holds the key before the time");
			stepNode->advance(0.5f, 1.0f, events);
			check(near_equal(sampled(*stepNode, skeleton).T[0].x, 10.0f), "step switches on the key time");

			// cubic spline, glTF layout per key: in tangent, value, out tangent
			AnimationClipData cubic;
			cubic.Name = "cubic";
			cubic.Duration = 1.0f;
			cubic.Channels.push_back(make_channel(10, AnimationPath::Translation, AnimationInterpolation::CubicSpline, { 0, 1 },
				{ 0, 0, 0, /*value*/ 0, 0, 0, /*out*/ 4, 0, 0,   /*in*/ 0, 0, 0, /*value*/ 1, 0, 0, /*out*/ 0, 0, 0 }));
			auto cubicNode = make_clip_node(cubic, skeleton);
			cubicNode->advance(0.5f, 1.0f, events);
			check(near_equal(sampled(*cubicNode, skeleton).T[0].x, 1.0f), "cubic spline uses the tangents (hermite at the middle)");
			cubicNode->advance(0.5f, 1.0f, events);
			check(near_equal(sampled(*cubicNode, skeleton).T[0].x, 1.0f), "cubic spline ends on the last value");

			// rotations are read as x y z w
			AnimationClipData turn;
			turn.Name = "turn";
			turn.Duration = 1.0f;
			const float half = std::sin(glm::radians(45.0f));
			turn.Channels.push_back(make_channel(11, AnimationPath::Rotation, AnimationInterpolation::Linear, { 0, 1 },
				{ 0, 0, 0, 1, 0, 0, half, std::cos(glm::radians(45.0f)) }));
			auto turnNode = make_clip_node(turn, skeleton);
			turnNode->advance(0.5f, 1.0f, events);
			check(near_equal(glm::degrees(glm::angle(sampled(*turnNode, skeleton).R[1])), 45.0f, 0.01f), "a rotation key is slerped: half of 90 degrees");

			// a clip that has nothing, and one with no length
			AnimationClipData empty;
			empty.Name = "empty";
			auto emptyNode = make_clip_node(empty, skeleton, true);
			emptyNode->advance(1.0f, 1.0f, events);
			const Pose emptyPose = sampled(*emptyNode, skeleton);
			check(near_equal(emptyPose.T[0].y, 1.0f) && events.empty(), "an empty clip evaluates to the rest pose");

			// a channel for a node the skeleton does not have is ignored
			AnimationClipData foreign = linear;
			foreign.Channels.push_back(x_channel(99, AnimationInterpolation::Linear, { 0, 1 }, { 0, 1 }));
			const ClipBinding binding = bind_clip(foreign, skeleton);
			check(binding.JointOfChannel == std::vector<i32>({ 0, -1 }) && binding.Unbound == 1, "a channel outside the skeleton is not bound");
			auto foreignNode = make_clip_node(foreign, skeleton);
			foreignNode->advance(1.0f, 1.0f, events);
			check(near_equal(sampled(*foreignNode, skeleton).T[0].x, 10.0f), "a clip with a foreign channel still plays");
		}

		void blending(AnimCheck& check)
		{
			const SkeletonData skeleton = make_skeleton();
			std::vector<ClipEvent> events;

			AnimationClipData a, b;
			a.Name = "a";
			b.Name = "b";
			a.Channels.push_back(x_channel(10, AnimationInterpolation::Step, { 0 }, { 2 }));
			b.Channels.push_back(x_channel(10, AnimationInterpolation::Step, { 0 }, { 6 }));

			for (const float weight : { 0.0f, 0.5f, 1.0f })
			{
				BlendNode blend(make_clip_node(a, skeleton), make_clip_node(b, skeleton), weight);
				check(near_equal(sampled(blend, skeleton).T[0].x, 2.0f + 4.0f * weight), "blend of translation at weight " + std::to_string(weight));
			}

			// the shortest way: q and -q are the same rotation, halfway between them is that rotation too
			AnimationClipData qa, qb;
			qa.Name = "qa";
			qb.Name = "qb";
			qa.Channels.push_back(make_channel(10, AnimationPath::Rotation, AnimationInterpolation::Step, { 0 }, { 0, 0, 0, 1 }));
			qb.Channels.push_back(make_channel(10, AnimationPath::Rotation, AnimationInterpolation::Step, { 0 }, { 0, 0, 0, -1 }));
			BlendNode opposite(make_clip_node(qa, skeleton), make_clip_node(qb, skeleton), 0.5f);
			const glm::quat mid = sampled(opposite, skeleton).R[0];
			check(near_equal(std::abs(mid.w), 1.0f), "blending q and -q does not collapse the rotation");

			AnimationClipData ninety;
			ninety.Name = "ninety";
			ninety.Channels.push_back(make_channel(10, AnimationPath::Rotation, AnimationInterpolation::Step, { 0 }, { 0, 0, std::sin(glm::radians(45.0f)), std::cos(glm::radians(45.0f)) }));
			BlendNode quarter(make_clip_node(qa, skeleton), make_clip_node(ninety, skeleton), 0.5f);
			check(near_equal(glm::degrees(glm::angle(sampled(quarter, skeleton).R[0])), 45.0f, 0.5f), "nlerp halfway to 90 degrees is about 45");

			// additive: the difference of a clip from its own first frame, on top of a base
			AnimationClipData base, add;
			base.Name = "base";
			add.Name = "add";
			add.Duration = 1.0f;
			base.Channels.push_back(x_channel(10, AnimationInterpolation::Step, { 0 }, { 2 }));
			add.Channels.push_back(x_channel(10, AnimationInterpolation::Linear, { 0, 1 }, { 1, 4 }));
			for (const float weight : { 0.0f, 0.5f, 1.0f })
			{
				AdditiveNode additive(make_clip_node(base, skeleton), make_clip_node(add, skeleton), weight);
				additive.advance(1.0f, 1.0f, events);
				check(near_equal(sampled(additive, skeleton).T[0].x, 2.0f + 3.0f * weight), "additive translation at weight " + std::to_string(weight));
			}
			AdditiveNode atStart(make_clip_node(base, skeleton), make_clip_node(add, skeleton), 1.0f);
			check(near_equal(sampled(atStart, skeleton).T[0].x, 2.0f), "an additive clip on its first frame changes nothing");

			// a mask: the layer only reaches mid and tip
			AnimationClipData low, high;
			low.Name = "low";
			high.Name = "high";
			for (const u32 node : { 10u, 11u, 12u })
			{
				low.Channels.push_back(x_channel(node, AnimationInterpolation::Step, { 0 }, { 1 }));
				high.Channels.push_back(x_channel(node, AnimationInterpolation::Step, { 0 }, { 9 }));
			}
			MaskNode masked(make_clip_node(low, skeleton), make_clip_node(high, skeleton), subtree_mask(skeleton, 1), 1.0f);
			const Pose maskedPose = sampled(masked, skeleton);
			check(near_equal(maskedPose.T[0].x, 1.0f) && near_equal(maskedPose.T[1].x, 9.0f) && near_equal(maskedPose.T[2].x, 9.0f), "a mask limits a layer to a joint and its subtree");

			// the same tree evaluated twice gives the same bytes
			const Pose first = sampled(masked, skeleton);
			const Pose second = sampled(masked, skeleton);
			check(std::memcmp(first.T.data(), second.T.data(), first.T.size() * sizeof(glm::vec3)) == 0
				&& std::memcmp(first.R.data(), second.R.data(), first.R.size() * sizeof(glm::quat)) == 0, "evaluation is deterministic");
		}

		AnimationClipData clip_with_events(const char* name, std::vector<AnimationEvent> events)
		{
			AnimationClipData clip;
			clip.Name = name;
			clip.Duration = 1.0f;
			clip.Events = std::move(events);
			return clip;
		}

		std::string joined(const std::vector<ClipEvent>& events)
		{
			std::string text;
			for (const ClipEvent& event : events)
			{
				text += (text.empty() ? "" : ",") + event.Name;
			}
			return text;
		}

		void clip_events(AnimCheck& check)
		{
			const SkeletonData skeleton = make_skeleton();
			const AnimationClipData clip = clip_with_events("walk", { { 0.0f, "start" }, { 0.05f, "c" }, { 0.25f, "a" }, { 0.9f, "b" } });

			auto looping = make_clip_node(clip, skeleton, true);
			std::vector<ClipEvent> events;
			looping->advance(0.1f, 1.0f, events);
			check(joined(events) == "start,c", "an event at time 0 fires on the first advance");

			events.clear();
			looping->advance(0.7f, 1.0f, events);
			check(joined(events) == "a", "an event fires once inside the interval played");

			events.clear();
			looping->advance(0.3f, 1.0f, events); // 0.8 -> 1.1: b, then the wrap to 0.1 passes c
			check(joined(events) == "b,start,c", "events fire across a loop wrap, in play order");
			check(events.size() == 3 && events[0].Clip == "walk" && near_equal(events[0].Time, 0.9f), "an event carries its clip and time");

			auto once = make_clip_node(clip, skeleton, false);
			events.clear();
			once->advance(3.0f, 1.0f, events);
			check(joined(events) == "start,c,a,b" && once->finished(), "a clip played once fires every event up to the end");
			events.clear();
			once->advance(1.0f, 1.0f, events);
			check(events.empty(), "a finished clip fires nothing more");

			// only the side of a crossfade that is mostly in the pose fires
			const AnimationClipData left = clip_with_events("left", { { 0.5f, "step" } });
			const AnimationClipData right = clip_with_events("right", { { 0.5f, "step" } });
			for (const auto& [weight, expected] : { std::pair<float, std::string>{ 0.5f, "" }, { 0.6f, "right" }, { 0.4f, "left" } })
			{
				BlendNode blend(make_clip_node(left, skeleton), make_clip_node(right, skeleton), weight);
				events.clear();
				blend.advance(0.6f, 1.0f, events);
				std::string clips;
				for (const ClipEvent& event : events)
				{
					clips += event.Clip;
				}
				check(clips == expected, "crossfade at " + std::to_string(weight) + " fires '" + expected + "' and not both sides");
			}

			// a clip with no length plays without events or a crash
			AnimationClipData flat;
			flat.Name = "flat";
			flat.Events = { { 0.0f, "never" } };
			auto flatNode = make_clip_node(flat, skeleton, true);
			events.clear();
			flatNode->advance(1.0f, 1.0f, events);
			check(events.empty(), "a clip of no length fires no events");
		}
	}

	void run_pose_checks(AnimCheck& check)
	{
		rest_and_hierarchy(check);
		folded_nodes(check);
		sampling(check);
		blending(check);
		clip_events(check);
	}
}
