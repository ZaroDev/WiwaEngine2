#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "clip_binding.h"
#include "graph_nodes.h"

namespace wiwa
{
	enum class CompileStatus
	{
		Ready,
		Pending, // a clip the graph names has not loaded yet: try again later
		Failed,  // the graph cannot be played on this skeleton (Error says why)
	};

	// A graph turned into the pose nodes an animator evaluates: Root is the base layer's machine with the other layers over it, Params holds the
	// parameter values (the nodes point to it), Machines is one machine per layer (the pointers stay valid as long as Root lives) for state
	// readouts, and WeightUpdates sets the weights of layers that follow a parameter (run before each advance).
	struct CompiledGraph
	{
		CompileStatus Status = CompileStatus::Pending;
		std::string Error{};
		std::unique_ptr<PoseNode> Root{};
		std::unique_ptr<GraphParams> Params{};
		std::vector<StateMachineNode*> Machines{};
		std::vector<std::string> LayerNames{};
		std::vector<std::function<void()>> WeightUpdates{};
	};

	// Returns null for a clip that has not loaded (or failed). The binding cache is the instance's: one binding per (clip, skeleton).
	using GraphClipSource = std::function<const AnimationClipData*(const AssetRef&)>;

	CompiledGraph compile_graph(const AnimationGraphData& graph, const SkeletonData& skeleton, const GraphClipSource& clips,
		std::map<const AnimationClipData*, std::shared_ptr<const ClipBinding>>& bindings);
}
