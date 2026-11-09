#include <pch.h>
#include "graph_compiler.h"

#include <assets/native_anim_files.h>

#include "animation_log.h"
#include "pose.h"

namespace wiwa
{
	namespace
	{
		// The weight of each joint for a layer: the subtree of MaskRoot (dropping by the falloff per joint of depth), the named joints, or 1 everywhere.
		std::vector<float> layer_mask(const GraphLayer& layer, const SkeletonData& skeleton)
		{
			const size_t count = skeleton.JointNames.size();
			if (!layer.MaskRoot.empty())
			{
				std::vector<float> mask(count, 0.0f);
				const i32 root = find_joint(skeleton, layer.MaskRoot);
				if (root < 0)
				{
					WI_ANIM_LOG(Warning, "Graph layer '{0}': the mask root '{1}' is not a joint of the skeleton; the layer reaches nothing", layer.Name, layer.MaskRoot);
					return mask;
				}
				for (size_t j = 0; j < count; j++)
				{
					int depth = 0;
					i32 at = static_cast<i32>(j);
					while (at >= 0 && at != root)
					{
						at = skeleton.Parents[static_cast<size_t>(at)];
						depth++;
					}
					if (at == root)
					{
						mask[j] = std::max(0.0f, 1.0f - layer.MaskFalloff * static_cast<float>(depth));
					}
				}
				return mask;
			}
			if (!layer.MaskJoints.empty())
			{
				std::vector<float> mask(count, 0.0f);
				for (const GraphMaskJoint& entry : layer.MaskJoints)
				{
					const i32 joint = find_joint(skeleton, entry.Joint);
					if (joint < 0)
					{
						WI_ANIM_LOG(Warning, "Graph layer '{0}': the mask joint '{1}' is not a joint of the skeleton", layer.Name, entry.Joint);
						continue;
					}
					mask[static_cast<size_t>(joint)] = entry.Weight;
				}
				return mask;
			}
			return std::vector<float>(count, 1.0f);
		}

		int state_index(const GraphLayer& layer, const std::string& name)
		{
			for (size_t i = 0; i < layer.States.size(); i++)
			{
				if (layer.States[i].Name == name)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}
	}

	CompiledGraph compile_graph(const AnimationGraphData& graph, const SkeletonData& skeleton, const GraphClipSource& clips,
		std::map<const AnimationClipData*, std::shared_ptr<const ClipBinding>>& bindings)
	{
		CompiledGraph result;
		std::string error;
		if (!graph_valid(graph, error))
		{
			result.Status = CompileStatus::Failed;
			result.Error = error;
			return result;
		}

		// a clip that has not loaded makes the whole graph wait: nothing is half built
		std::map<AssetRef, const AnimationClipData*, bool (*)(const AssetRef&, const AssetRef&)> loaded(
			[](const AssetRef& a, const AssetRef& b) { return a.Guid.Hi != b.Guid.Hi ? a.Guid.Hi < b.Guid.Hi : (a.Guid.Lo != b.Guid.Lo ? a.Guid.Lo < b.Guid.Lo : a.LocalId < b.LocalId); });
		const auto find_clip = [&](const AssetRef& ref) -> const AnimationClipData*
		{
			const auto it = loaded.find(ref);
			if (it != loaded.end())
			{
				return it->second;
			}
			const AnimationClipData* clip = clips(ref);
			if (clip != nullptr)
			{
				loaded.emplace(ref, clip);
			}
			return clip;
		};
		for (const GraphLayer& layer : graph.Layers)
		{
			for (const GraphState& state : layer.States)
			{
				if (state.Body == GraphBodyKind::Clip && find_clip(state.Clip) == nullptr)
				{
					result.Status = CompileStatus::Pending;
					return result;
				}
				for (const GraphBlendSample& sample : state.Samples)
				{
					if (state.Body == GraphBodyKind::Blend && find_clip(sample.Clip) == nullptr)
					{
						result.Status = CompileStatus::Pending;
						return result;
					}
				}
			}
		}

		const auto make_clip = [&](const AnimationClipData& clip) -> std::unique_ptr<ClipNode>
		{
			auto& binding = bindings[&clip];
			if (!binding)
			{
				binding = std::make_shared<const ClipBinding>(bind_clip(clip, skeleton));
			}
			return std::make_unique<ClipNode>(clip, skeleton, binding);
		};

		result.Params = std::make_unique<GraphParams>(graph.Parameters);
		GraphParams& params = *result.Params;
		const Pose rest = rest_pose(skeleton);

		for (size_t l = 0; l < graph.Layers.size(); l++)
		{
			const GraphLayer& layer = graph.Layers[l];
			StateMachineSetup setup;
			setup.Rest = rest;
			setup.Default = state_index(layer, layer.DefaultState);
			for (const GraphState& state : layer.States)
			{
				StateMachineSetup::State built;
				built.Name = state.Name;
				built.Speed = state.Speed;
				built.SpeedParam = state.SpeedParameter.empty() ? -1 : params.index_of(state.SpeedParameter);
				if (state.Body == GraphBodyKind::Clip)
				{
					built.Body = std::make_unique<ClipBody>(make_clip(*find_clip(state.Clip)), state.Loop);
				}
				else
				{
					std::vector<std::unique_ptr<ClipNode>> samples;
					std::vector<glm::vec2> points;
					for (const GraphBlendSample& sample : state.Samples)
					{
						samples.push_back(make_clip(*find_clip(sample.Clip)));
						points.emplace_back(sample.X, sample.Y);
					}
					built.Body = std::make_unique<BlendSpaceNode>(std::move(samples), std::move(points), state.Loop, params, params.index_of(state.BlendX),
						state.BlendY.empty() ? -1 : params.index_of(state.BlendY));
				}
				setup.States.push_back(std::move(built));
			}
			for (const GraphTransition& transition : layer.Transitions)
			{
				StateMachineSetup::Transition built;
				built.From = transition.From.empty() ? -1 : state_index(layer, transition.From);
				built.To = state_index(layer, transition.To);
				built.Duration = transition.Duration;
				built.ExitTime = transition.ExitTime;
				for (const GraphCondition& condition : transition.Conditions)
				{
					built.Conditions.push_back({ params.index_of(condition.Parameter), condition.Op, condition.Value });
				}
				setup.Transitions.push_back(std::move(built));
			}

			auto machine = std::make_unique<StateMachineNode>(std::move(setup), params);
			result.Machines.push_back(machine.get());
			result.LayerNames.push_back(layer.Name);
			const int weightParam = layer.WeightParameter.empty() ? -1 : params.index_of(layer.WeightParameter);

			if (l == 0)
			{
				result.Root = std::move(machine);
				continue;
			}
			std::vector<float> mask = layer_mask(layer, skeleton);
			if (layer.Mode == GraphLayerMode::Additive)
			{
				auto node = std::make_unique<AdditiveMachineNode>(std::move(result.Root), std::move(machine), layer.Weight, std::move(mask));
				if (weightParam >= 0)
				{
					AdditiveMachineNode* raw = node.get();
					result.WeightUpdates.push_back([raw, &params, weightParam]() { raw->set_weight(params.get(weightParam)); });
				}
				result.Root = std::move(node);
			}
			else
			{
				auto node = std::make_unique<MaskNode>(std::move(result.Root), std::move(machine), std::move(mask), layer.Weight);
				if (weightParam >= 0)
				{
					MaskNode* raw = node.get();
					result.WeightUpdates.push_back([raw, &params, weightParam]() { raw->set_weight(params.get(weightParam)); });
				}
				result.Root = std::move(node);
			}
		}

		for (const std::function<void()>& update : result.WeightUpdates)
		{
			update(); // the weights the parameters' defaults give
		}
		result.Status = CompileStatus::Ready;
		return result;
	}
}
