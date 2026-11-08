#include <pch.h>
#include "animator_instance.h"

#include <cmath>

#include "animation_log.h"

namespace wiwa
{
	AnimatorInstance::AnimatorInstance(const AnimationSources& sources, const Animator& config)
		: m_Sources(sources), m_Clips(config.Clips), m_SkeletonRef(config.Skeleton), m_Root(std::make_unique<RestNode>())
	{
		m_GraphRef = config.Graph;
		m_FallbackClip = config.Clip;
		m_FallbackPlay = config.PlayOnStart;
	}

	void AnimatorInstance::submit(Command command)
	{
		if (command.Kind == CommandKind::Play || command.Kind == CommandKind::Crossfade || command.Kind == CommandKind::Stop)
		{
			// the base is the script's now: the graph does not come back
			if (m_GraphMode)
			{
				warn_once("leave", "A script took over the base pose of an Animator that plays a graph (play, crossfade or stop): the graph ends");
			}
			leave_graph();
			m_GraphGaveUp = true;
		}
		m_Commands.push_back(std::move(command));
		run_commands();
	}

	void AnimatorInstance::play(ClipSelector clip, PlayOptions options)
	{
		Command command;
		command.Kind = CommandKind::Play;
		command.Clip = std::move(clip);
		command.Options = options;
		submit(std::move(command));
	}

	void AnimatorInstance::crossfade(ClipSelector clip, float seconds, PlayOptions options)
	{
		Command command;
		command.Kind = CommandKind::Crossfade;
		command.Clip = std::move(clip);
		command.Options = options;
		command.Seconds = seconds;
		submit(std::move(command));
	}

	void AnimatorInstance::stop(float seconds)
	{
		Command command;
		command.Kind = CommandKind::Stop;
		command.Seconds = seconds;
		submit(std::move(command));
	}

	LayerId AnimatorInstance::add_layer(const std::string& name, ClipSelector clip, const LayerOptions& options)
	{
		Command command;
		command.Kind = CommandKind::AddLayer;
		command.Clip = std::move(clip);
		command.LayerName = name;
		command.LayerOpts = options;
		command.Layer = m_NextLayer++;
		const LayerId id = command.Layer;
		submit(std::move(command));
		return id;
	}

	void AnimatorInstance::layer_crossfade(LayerId layer, ClipSelector clip, float seconds, bool loop)
	{
		Command command;
		command.Kind = CommandKind::LayerCrossfade;
		command.Clip = std::move(clip);
		command.Seconds = seconds;
		command.Layer = layer;
		command.Options.Loop = loop;
		submit(std::move(command));
	}

	void AnimatorInstance::layer_stop(LayerId layer, float seconds)
	{
		Command command;
		command.Kind = CommandKind::LayerStop;
		command.Seconds = seconds;
		command.Layer = layer;
		submit(std::move(command));
	}

	void AnimatorInstance::seek(float time)
	{
		if (m_Current != nullptr)
		{
			m_Current->set_time(time);
		}
	}

	AnimatorInstance::Layer* AnimatorInstance::find_layer(LayerId id)
	{
		for (Layer& layer : m_Layers)
		{
			if (layer.Id == id)
			{
				return &layer;
			}
		}
		return nullptr;
	}

	const AnimatorInstance::Layer* AnimatorInstance::find_layer(LayerId id) const
	{
		return const_cast<AnimatorInstance*>(this)->find_layer(id);
	}

	void AnimatorInstance::set_layer_weight(LayerId layer, float weight)
	{
		if (Layer* found = find_layer(layer))
		{
			found->Weight = std::clamp(weight, 0.0f, 1.0f);
			found->Stopping = false;
		}
	}

	float AnimatorInstance::layer_weight(LayerId layer) const
	{
		const Layer* found = find_layer(layer);
		return found != nullptr ? found->Weight : 0.0f;
	}

	bool AnimatorInstance::layer_alive(LayerId layer) const
	{
		if (find_layer(layer) != nullptr)
		{
			return true;
		}
		for (const Command& command : m_Commands)
		{
			if (command.Kind == CommandKind::AddLayer && command.Layer == layer)
			{
				return true;
			}
		}
		return false;
	}

	ClipLookup AnimatorInstance::lookup(const std::string& name, AssetRef& out) const
	{
		bool pending = false;
		for (const AssetRef& ref : m_Clips)
		{
			const AnimationClipData* clip = m_Sources.Clip ? m_Sources.Clip(ref) : nullptr;
			if (clip == nullptr)
			{
				pending = true;
			}
			else if (clip->Name == name)
			{
				out = ref;
				return ClipLookup::Found;
			}
		}
		return pending ? ClipLookup::Pending : ClipLookup::Unknown;
	}

	std::vector<std::string> AnimatorInstance::clip_names() const
	{
		std::vector<std::string> names;
		for (const AssetRef& ref : m_Clips)
		{
			if (const AnimationClipData* clip = m_Sources.Clip ? m_Sources.Clip(ref) : nullptr)
			{
				names.push_back(clip->Name);
			}
		}
		return names;
	}

	// True when the command can run now. A clip that never will exist (an unknown name) sets unknown; the command is dropped.
	bool AnimatorInstance::resolve(const ClipSelector& selector, const AnimationClipData*& out, bool& unknown) const
	{
		out = nullptr;
		unknown = false;
		AssetRef ref = selector.Ref;
		if (!ref.valid())
		{
			switch (lookup(selector.Name, ref))
			{
			case ClipLookup::Found: break;
			case ClipLookup::Pending: return false;
			case ClipLookup::Unknown: unknown = true; return true;
			}
		}
		out = m_Sources.Clip ? m_Sources.Clip(ref) : nullptr;
		return out != nullptr;
	}

	std::unique_ptr<ClipNode> AnimatorInstance::make_clip(const AnimationClipData& clip, const PlayOptions& options)
	{
		std::shared_ptr<const ClipBinding>& binding = m_Bindings[&clip];
		if (binding == nullptr)
		{
			binding = std::make_shared<const ClipBinding>(bind_clip(clip, *m_Skeleton));
			if (clip_compatibility(clip, *m_Skeleton).Result == ClipCompatibility::None)
			{
				WI_ANIM_LOG(Warning, "The clip {0} has no joint in common with the skeleton {1}: it moves nothing", clip.Name, m_Skeleton->Name);
			}
		}
		auto node = std::make_unique<ClipNode>(clip, *m_Skeleton, binding);
		node->set_loop(options.Loop);
		node->set_speed(options.Speed);
		return node;
	}

	void AnimatorInstance::run_commands()
	{
		if (m_Skeleton == nullptr && m_Sources.Skeleton)
		{
			m_Skeleton = m_Sources.Skeleton(m_SkeletonRef);
		}
		if (m_Skeleton == nullptr)
		{
			return; // the commands wait for the skeleton
		}

		while (!m_Commands.empty())
		{
			const Command& command = m_Commands.front();
			const AnimationClipData* clip = nullptr;
			bool unknown = false;
			const bool needsClip = command.Kind == CommandKind::Play || command.Kind == CommandKind::Crossfade
				|| command.Kind == CommandKind::AddLayer || command.Kind == CommandKind::LayerCrossfade;
			if (needsClip && !resolve(command.Clip, clip, unknown))
			{
				return; // in order: the commands behind this one wait too
			}

			if (unknown)
			{
				WI_ANIM_LOG(Error, "The animator has no clip named '{0}'", command.Clip.Name);
			}
			else
			{
				apply(command, clip);
			}
			m_Commands.pop_front();
		}
	}

	void AnimatorInstance::apply(const Command& command, const AnimationClipData* clip)
	{
		switch (command.Kind)
		{
		case CommandKind::Play:
		{
			std::unique_ptr<ClipNode> node = make_clip(*clip, command.Options);
			m_Current = node.get();
			m_Root = std::move(node);
			break;
		}
		case CommandKind::Crossfade:
		{
			if (command.Seconds <= 0.0f)
			{
				std::unique_ptr<ClipNode> node = make_clip(*clip, command.Options);
				m_Current = node.get();
				m_Root = std::move(node);
				break;
			}
			if (m_Root->node_count() >= ANIMATOR_MAX_TREE_NODES)
			{
				m_Root->settle();
				if (std::unique_ptr<PoseNode> replacement = m_Root->collapse())
				{
					m_Root = std::move(replacement);
				}
			}
			std::unique_ptr<ClipNode> node = make_clip(*clip, command.Options);
			m_Current = node.get();
			auto blend = std::make_unique<BlendNode>(std::move(m_Root), std::move(node), 0.0f);
			blend->ramp(command.Seconds);
			m_Root = std::move(blend);
			break;
		}
		case CommandKind::Stop:
		{
			m_Current = nullptr;
			if (command.Seconds <= 0.0f)
			{
				m_Root = std::make_unique<RestNode>();
				break;
			}
			auto blend = std::make_unique<BlendNode>(std::move(m_Root), std::make_unique<RestNode>(), 0.0f);
			blend->ramp(command.Seconds);
			m_Root = std::move(blend);
			break;
		}
		case CommandKind::AddLayer:
		{
			std::erase_if(m_Layers, [&](const Layer& layer) { return layer.Name == command.LayerName; }); // the same name replaces the layer

			Layer layer;
			layer.Id = command.Layer;
			layer.Name = command.LayerName;
			layer.Weight = std::clamp(command.LayerOpts.Weight, 0.0f, 1.0f);
			layer.Additive = command.LayerOpts.Additive;
			if (!command.LayerOpts.Mask.empty())
			{
				const i32 joint = find_joint(*m_Skeleton, command.LayerOpts.Mask);
				if (joint < 0)
				{
					WI_ANIM_LOG(Warning, "The layer {0} masks the joint '{1}', which the skeleton does not have: it reaches the whole body", command.LayerName, command.LayerOpts.Mask);
				}
				else
				{
					layer.Mask = subtree_mask(*m_Skeleton, joint);
				}
			}

			PlayOptions options;
			options.Loop = command.LayerOpts.Loop;
			if (layer.Additive)
			{
				layer.Clip = make_clip(*clip, options);
			}
			else
			{
				layer.Tree = make_clip(*clip, options);
			}
			m_Layers.push_back(std::move(layer));
			break;
		}
		case CommandKind::LayerCrossfade:
		{
			Layer* layer = find_layer(command.Layer);
			if (layer == nullptr)
			{
				break;
			}
			layer->Stopping = false;
			if (layer->Additive)
			{
				// an additive layer has one clip: it is replaced (it has no reference frame to blend between)
				layer->Clip = make_clip(*clip, command.Options);
				break;
			}
			std::unique_ptr<ClipNode> node = make_clip(*clip, command.Options);
			if (command.Seconds <= 0.0f)
			{
				layer->Tree = std::move(node);
				break;
			}
			if (layer->Tree->node_count() >= ANIMATOR_MAX_TREE_NODES)
			{
				layer->Tree->settle();
				if (std::unique_ptr<PoseNode> replacement = layer->Tree->collapse())
				{
					layer->Tree = std::move(replacement);
				}
			}
			auto blend = std::make_unique<BlendNode>(std::move(layer->Tree), std::move(node), 0.0f);
			blend->ramp(command.Seconds);
			layer->Tree = std::move(blend);
			break;
		}
		case CommandKind::LayerStop:
		{
			Layer* layer = find_layer(command.Layer);
			if (layer == nullptr)
			{
				break;
			}
			if (command.Seconds <= 0.0f || layer->Weight <= 0.0f)
			{
				std::erase_if(m_Layers, [&](const Layer& other) { return other.Id == command.Layer; });
				break;
			}
			layer->Stopping = true;
			layer->FadeRate = layer->Weight / command.Seconds;
			break;
		}
		}
	}

	u32 AnimatorInstance::node_count() const
	{
		u32 count = m_Root->node_count();
		for (const Layer& layer : m_Layers)
		{
			count += layer.Additive ? 1 : layer.Tree->node_count();
		}
		return count;
	}

	void AnimatorInstance::update(float deltaTime, std::vector<ClipEvent>& events)
	{
		run_commands();
		if (m_Skeleton == nullptr)
		{
			return;
		}
		try_compile_graph();

		if (m_GraphMode)
		{
			for (const std::function<void()>& update : m_Graph.WeightUpdates)
			{
				update();
			}
			m_Root->advance(deltaTime, 1.0f, events);
			m_Graph.Params->end_frame(); // a trigger lives for one frame; the nodes that were to use it did
			// no collapse: a layer at weight 0 stays, its weight follows a parameter
		}
		else
		{
			m_Root->advance(deltaTime, 1.0f, events);
			if (std::unique_ptr<PoseNode> replacement = m_Root->collapse())
			{
				m_Root = std::move(replacement);
			}
		}

		for (Layer& layer : m_Layers)
		{
			if (layer.Stopping)
			{
				layer.Weight = std::max(0.0f, layer.Weight - layer.FadeRate * std::max(deltaTime, 0.0f));
			}
			if (layer.Additive)
			{
				layer.Clip->advance(deltaTime, layer.Weight, events);
			}
			else
			{
				layer.Tree->advance(deltaTime, layer.Weight, events);
				if (std::unique_ptr<PoseNode> replacement = layer.Tree->collapse())
				{
					layer.Tree = std::move(replacement);
				}
			}
		}
		std::erase_if(m_Layers, [](const Layer& layer) { return layer.Stopping && layer.Weight <= 0.0f; });

		const Pose rest = rest_pose(*m_Skeleton);
		Pose pose = rest;
		m_Root->evaluate(pose);
		for (const Layer& layer : m_Layers)
		{
			const std::vector<float>* mask = layer.Mask.empty() ? nullptr : &layer.Mask;
			if (layer.Additive)
			{
				Pose current = rest;
				layer.Clip->evaluate(current);
				Pose reference = rest;
				layer.Clip->sample(0.0f, reference);
				add_pose(pose, current, reference, layer.Weight, mask, pose);
			}
			else
			{
				Pose layerPose = rest;
				layer.Tree->evaluate(layerPose);
				blend_poses(pose, layerPose, layer.Weight, mask, pose);
			}
		}

		m_Pose = std::move(pose);
		model_matrices(*m_Skeleton, m_Pose, m_Model);
		m_PreviousPalette = m_Palette;
		build_palette(*m_Skeleton, m_Model, m_Palette);
		if (m_PreviousPalette.size() != m_Palette.size())
		{
			m_PreviousPalette = m_Palette; // the first frame has no history
		}
	}

	void AnimatorInstance::warn_once(const std::string& key, const std::string& message)
	{
		if (m_Warned.insert(key).second)
		{
			WI_ANIM_LOG(Warning, "{0}", message);
		}
	}

	void AnimatorInstance::give_up_graph(const std::string& reason)
	{
		m_GraphGaveUp = true;
		m_QueuedParams.clear();
		WI_ANIM_LOG(Warning, "The animation graph cannot play on this Animator: {0}", reason);
		if (m_FallbackPlay && m_FallbackClip.valid())
		{
			play(ClipSelector{ {}, m_FallbackClip });
		}
	}

	void AnimatorInstance::leave_graph()
	{
		if (!m_GraphMode)
		{
			return;
		}
		m_Root = std::make_unique<RestNode>(); // the nodes point to the parameters: they go first
		m_Graph = CompiledGraph{};
		m_GraphMode = false;
	}

	// A graph, a clip or a skeleton that never loads (deleted, not what its reference says) leaves the Animator in the rest pose: say so once.
	void AnimatorInstance::note_pending()
	{
		if (++m_PendingFrames == 600)
		{
			warn_once("pending", "The Animator has waited for its animation graph (or a skeleton or clip it names) for 600 frames: it may be missing or failed to load; the rest pose plays meanwhile");
		}
	}

	void AnimatorInstance::try_compile_graph()
	{
		if (!m_GraphRef.valid() || m_GraphMode || m_GraphGaveUp || !m_Sources.Graph || m_Skeleton == nullptr)
		{
			return;
		}
		const AnimationGraphData* graph = m_Sources.Graph(m_GraphRef);
		if (graph == nullptr)
		{
			note_pending(); // loading
			return;
		}
		if (graph->Skeleton.valid())
		{
			// the same joints (not the same reference: a legacy reference and the extracted skeleton are one rig)
			const SkeletonData* made = m_Sources.Skeleton ? m_Sources.Skeleton(graph->Skeleton) : nullptr;
			if (made == nullptr)
			{
				note_pending();
				return;
			}
			if (made != m_Skeleton && made->JointNames != m_Skeleton->JointNames)
			{
				give_up_graph("it was made for another skeleton than the Animator's");
				return;
			}
		}

		CompiledGraph compiled = compile_graph(*graph, *m_Skeleton, m_Sources.Clip, m_Bindings);
		if (compiled.Status == CompileStatus::Pending)
		{
			note_pending();
			return;
		}
		if (compiled.Status == CompileStatus::Failed)
		{
			give_up_graph(compiled.Error);
			return;
		}
		m_Graph = std::move(compiled);
		m_Root = std::move(m_Graph.Root);
		m_Current = nullptr;
		m_GraphMode = true;
		for (const QueuedParam& queued : m_QueuedParams)
		{
			apply_param(queued.Name, queued.Value, queued.Trigger);
		}
		m_QueuedParams.clear();
	}

	bool AnimatorInstance::apply_param(const std::string& name, float value, bool isTrigger)
	{
		const int index = m_Graph.Params->index_of(name);
		if (index < 0)
		{
			warn_once("p:" + name, "The animation graph has no parameter named '" + name + "'");
			return false;
		}
		const bool isTriggerParam = m_Graph.Params->type_of(index) == GraphParamType::Trigger;
		if (isTrigger != isTriggerParam)
		{
			warn_once("p:" + name, isTriggerParam ? "The parameter '" + name + "' is a trigger: fire it with trigger()" : "The parameter '" + name + "' is not a trigger");
			return false;
		}
		if (isTrigger)
		{
			m_Graph.Params->fire(index);
		}
		else
		{
			m_Graph.Params->set(index, value);
		}
		return true;
	}

	bool AnimatorInstance::set_param(const std::string& name, float value)
	{
		if (!std::isfinite(value))
		{
			warn_once("n:" + name, "The value for the graph parameter '" + name + "' is not a number");
			return false;
		}
		if (m_GraphMode)
		{
			return apply_param(name, value, false);
		}
		if (m_GraphRef.valid() && !m_GraphGaveUp)
		{
			if (m_QueuedParams.size() < 64)
			{
				m_QueuedParams.push_back({ name, value, false });
			}
			return true;
		}
		warn_once("g:" + name, "The Animator plays no graph: the parameter '" + name + "' is ignored");
		return false;
	}

	bool AnimatorInstance::trigger(const std::string& name)
	{
		if (m_GraphMode)
		{
			return apply_param(name, 0.0f, true);
		}
		if (m_GraphRef.valid() && !m_GraphGaveUp)
		{
			if (m_QueuedParams.size() < 64)
			{
				m_QueuedParams.push_back({ name, 0.0f, true });
			}
			return true;
		}
		warn_once("g:" + name, "The Animator plays no graph: the trigger '" + name + "' is ignored");
		return false;
	}

	std::optional<float> AnimatorInstance::param(const std::string& name) const
	{
		if (!m_GraphMode)
		{
			return std::nullopt;
		}
		const int index = m_Graph.Params->index_of(name);
		return index >= 0 ? std::optional<float>(m_Graph.Params->get(index)) : std::nullopt;
	}

	std::optional<GraphParamType> AnimatorInstance::param_type(const std::string& name) const
	{
		if (!m_GraphMode)
		{
			return std::nullopt;
		}
		const int index = m_Graph.Params->index_of(name);
		return index >= 0 ? std::optional<GraphParamType>(m_Graph.Params->type_of(index)) : std::nullopt;
	}

	int AnimatorInstance::layer_index(const std::string& name) const
	{
		for (size_t i = 0; i < m_Graph.LayerNames.size(); i++)
		{
			if (m_Graph.LayerNames[i] == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string AnimatorInstance::state_name(size_t layer) const
	{
		return m_GraphMode && layer < m_Graph.Machines.size() ? m_Graph.Machines[layer]->active_state() : std::string{};
	}
}
