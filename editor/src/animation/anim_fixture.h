#pragma once

#include <map>

#include <animation/animation_module.h>

// A rig and clips for the animation smoke tests: two joints, the prefab nodes 1 (hip) and 2 (hand), and the clips Idle, Run, Wave and Late
// (Late loads when LateLoaded is set).
namespace wiwa
{
	namespace
	{
	AssetRef make_ref(u64 id)
	{
		return AssetRef{ AssetGuid{ 0xA11C0000ull + id, id }, 0 };
	}

	AnimationChannel x_channel(u32 node, std::vector<float> times, const std::vector<float>& xs)
	{
		AnimationChannel channel;
		channel.Joint = node == 1 ? "hip" : (node == 2 ? "hand" : "node" + std::to_string(node));
		channel.Path = AnimationPath::Translation;
		channel.Interpolation = AnimationInterpolation::Linear;
		channel.Components = 3;
		channel.Times = std::move(times);
		for (float x : xs)
		{
			channel.Values.insert(channel.Values.end(), { x, 0.0f, 0.0f });
		}
		return channel;
	}

	// Two joints, the prefab nodes 1 (hip) and 2 (hand), and three clips: Idle (hip stays at x = 1), Run (hip runs from 0 to 10 in a
	// second, with an event), Wave (a layer: the hand goes to x = 5).
	struct Fixture
	{
		SkeletonData Skeleton{};
		std::map<AssetGuid, AnimationClipData> Clips{};
		AnimationSources Sources{};
		AssetRef SkeletonRef = make_ref(1);
		AssetRef IdleRef = make_ref(2);
		AssetRef RunRef = make_ref(3);
		AssetRef WaveRef = make_ref(4);
		AssetRef LateRef = make_ref(5);
		AssetRef GraphRef = make_ref(6);
		std::map<AssetGuid, AnimationGraphData> Graphs{};
		bool LateLoaded = false;

		Fixture()
		{
			Skeleton.Name = "rig";
			Skeleton.JointNames = { "hip", "hand" };
			Skeleton.Parents = { -1, 0 };
			Skeleton.RestTranslation = { glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };
			Skeleton.RestRotation = { glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };
			Skeleton.RestScale = { glm::vec3(1.0f), glm::vec3(1.0f) };
			Skeleton.InverseBind = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)) };

			AnimationClipData idle;
			idle.Name = "Idle";
			idle.Duration = 1.0f;
			idle.Channels.push_back(x_channel(1, { 0.0f }, { 1.0f }));
			Clips[IdleRef.Guid] = idle;

			AnimationClipData run;
			run.Name = "Run";
			run.Duration = 1.0f;
			run.Channels.push_back(x_channel(1, { 0.0f, 1.0f }, { 0.0f, 10.0f }));
			run.Events = { { 0.5f, "footstep" } };
			Clips[RunRef.Guid] = run;

			AnimationClipData wave;
			wave.Name = "Wave";
			wave.Duration = 1.0f;
			wave.Channels.push_back(x_channel(2, { 0.0f }, { 5.0f }));
			Clips[WaveRef.Guid] = wave;

			AnimationClipData late;
			late.Name = "Late";
			late.Duration = 1.0f;
			late.Channels.push_back(x_channel(1, { 0.0f }, { 7.0f }));
			Clips[LateRef.Guid] = late;

			Sources.Graph = [this](const AssetRef& ref) -> const AnimationGraphData*
			{
				auto it = Graphs.find(ref.Guid);
				return it != Graphs.end() ? &it->second : nullptr;
			};
			Sources.Skeleton = [this](const AssetRef& ref) -> const SkeletonData* { return ref == SkeletonRef ? &Skeleton : nullptr; };
			Sources.Clip = [this](const AssetRef& ref) -> const AnimationClipData*
			{
				if (ref == LateRef && !LateLoaded)
				{
					return nullptr;
				}
				auto it = Clips.find(ref.Guid);
				return it != Clips.end() ? &it->second : nullptr;
			};
		}

		// A graph of the rig: Idle and Run, Idle -> Run when the trigger "jump" fires; parameters speed (float), go (bool), jump (trigger).
		void add_graph()
		{
			AnimationGraphData graph;
			graph.Name = "hero";
			graph.Skeleton = SkeletonRef;
			graph.Parameters = { { "speed", GraphParamType::Float, 0.0f }, { "go", GraphParamType::Bool, 0.0f }, { "jump", GraphParamType::Trigger, 0.0f } };
			GraphLayer base;
			base.Name = "Base";
			base.DefaultState = "Idle";
			GraphState idle;
			idle.Name = "Idle";
			idle.Clip = IdleRef;
			GraphState run;
			run.Name = "Run";
			run.Clip = RunRef;
			base.States = { idle, run };
			GraphTransition toRun;
			toRun.From = "Idle";
			toRun.To = "Run";
			toRun.Duration = 0.0f;
			toRun.Conditions = { { "jump", GraphCompareOp::Fired, 0.0f } };
			base.Transitions = { toRun };
			graph.Layers = { base };
			Graphs[GraphRef.Guid] = graph;
		}

		Animator config() const
		{
			Animator animator;
			animator.Skeleton = SkeletonRef;
			animator.Clips = { IdleRef, RunRef, WaveRef, LateRef };
			animator.Clip = IdleRef;
			return animator;
		}

		entt::entity make_character(Scene& scene, const std::string& name = "hero") const
		{
			const entt::entity entity = scene.create(name);
			scene.registry().emplace<Animator>(entity, config());
			return entity;
		}
	};
	}
}
