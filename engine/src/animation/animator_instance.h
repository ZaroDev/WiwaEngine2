#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <assets/asset_guid.h>
#include <scene/components.h>

#include "graph_compiler.h"
#include "pose.h"
#include "pose_nodes.h"

namespace wiwa
{
	// How an animator finds its assets. Both return null while the asset is loading (or failed). The pointers must stay valid until the
	// animators are dropped (the module keeps the handles). Tests put their own.
	struct AnimationSources
	{
		std::function<const SkeletonData*(const AssetRef&)> Skeleton{};
		std::function<const AnimationClipData*(const AssetRef&)> Clip{};
		std::function<const AnimationGraphData*(const AssetRef&)> Graph{};
	};

	// A clip by name (one of the Animator's Clips) or by asset.
	struct ClipSelector
	{
		std::string Name{};
		AssetRef Ref{};
	};

	struct PlayOptions
	{
		bool Loop = true;
		float Speed = 1.0f;
	};

	struct LayerOptions
	{
		std::string Mask{};    // a joint name: the layer reaches that joint and its subtree. Empty: the whole body
		float Weight = 1.0f;
		bool Additive = false; // the layer adds the change of the clip from its first frame to the pose below
		bool Loop = false;
	};

	enum class ClipLookup
	{
		Found,
		Pending, // some clip of the animator has not loaded yet, so the name may still turn up
		Unknown,
	};

	using LayerId = u32;
	constexpr LayerId NO_LAYER = 0;
	constexpr u32 ANIMATOR_MAX_TREE_NODES = 24; // a crossfade started above this settles the older fades first

	// The runtime state of one Animator: the tree of pose nodes, the layers above it, the commands that wait for an asset, and the result
	// (the pose, the model space matrices and the palette). Main thread only. play/crossfade/stop and the layer calls queue a command that
	// runs as soon as the clips it needs are loaded (at once, if they are).
	class AnimatorInstance
	{
	public:
		AnimatorInstance(const AnimationSources& sources, const Animator& config);

		// Control. A name that is not one of the Clips is logged when the commands run; ask lookup() first to report it to a caller.
		void play(ClipSelector clip, PlayOptions options = {});
		void crossfade(ClipSelector clip, float seconds, PlayOptions options = {});
		void stop(float seconds);
		LayerId add_layer(const std::string& name, ClipSelector clip, const LayerOptions& options);
		void layer_crossfade(LayerId layer, ClipSelector clip, float seconds, bool loop);
		void layer_stop(LayerId layer, float seconds);
		void set_layer_weight(LayerId layer, float weight);
		float layer_weight(LayerId layer) const;
		bool layer_alive(LayerId layer) const;

		ClipLookup lookup(const std::string& name, AssetRef& out) const;
		// The names of the clips that have loaded.
		std::vector<std::string> clip_names() const;

		// Animation graph (Animator::Graph). The graph compiles once the skeleton, the graph and its clips have loaded; until then parameter writes wait
		// (and are checked when it compiles). A graph that cannot play here (another skeleton, an invalid file) is logged once and the Animator plays its
		// Clip instead. play, crossfade and stop leave graph mode for good. A write that cannot be applied (unknown name, wrong kind, not a number) is
		// logged once per name and ignored, and returns false.
		bool graph_mode() const { return m_GraphMode; }
		bool set_param(const std::string& name, float value);
		bool trigger(const std::string& name);
		std::optional<float> param(const std::string& name) const;
		std::optional<GraphParamType> param_type(const std::string& name) const;
		size_t layer_count() const { return m_GraphMode ? m_Graph.Machines.size() : 0; }
		// The parameters of the graph that plays (null otherwise), for tools that list them.
		const GraphParams* params() const { return m_GraphMode ? m_Graph.Params.get() : nullptr; }
		// -1 when there is no layer with this name.
		int layer_index(const std::string& name) const;
		// The active state of a layer of the graph; empty when there is none.
		std::string state_name(size_t layer) const;

		// Moves the clip that plays to a time (a scrub); the pose follows with the next update. Events between are not fired.
		void seek(float time);

		// Runs the waiting commands, moves time (events go to the list), evaluates and builds the palette.
		void update(float deltaTime, std::vector<ClipEvent>& events);

		// The skeleton has loaded.
		bool ready() const { return m_Skeleton != nullptr; }
		const SkeletonData* skeleton() const { return m_Skeleton; }
		// A clip is playing on the base (not finished, not stopped). time() is its time.
		bool playing() const { return m_Current != nullptr && !m_Current->finished(); }
		float time() const { return m_Current != nullptr ? m_Current->time() : 0.0f; }
		u32 node_count() const;
		size_t pending_commands() const { return m_Commands.size(); }

		const Pose& pose() const { return m_Pose; }
		const std::vector<glm::mat4>& model() const { return m_Model; }
		const std::vector<JointMatrix>& palette() const { return m_Palette; }
		const std::vector<JointMatrix>& previous_palette() const { return m_PreviousPalette; }

	private:
		struct Layer
		{
			LayerId Id = NO_LAYER;
			std::string Name{};
			std::unique_ptr<PoseNode> Tree{};  // not additive
			std::unique_ptr<ClipNode> Clip{};  // additive: the one clip
			std::vector<float> Mask{};
			float Weight = 1.0f;
			float FadeRate = 0.0f;             // weight per second toward 0 once stopped; the layer ends at 0
			bool Stopping = false;
			bool Additive = false;
		};

		enum class CommandKind { Play, Crossfade, Stop, AddLayer, LayerCrossfade, LayerStop };

		struct Command
		{
			CommandKind Kind = CommandKind::Play;
			ClipSelector Clip{};
			PlayOptions Options{};
			float Seconds = 0.0f;
			LayerId Layer = NO_LAYER;
			std::string LayerName{};
			LayerOptions LayerOpts{};
		};

		void submit(Command command);
		void try_compile_graph();
		void note_pending();
		void give_up_graph(const std::string& reason);
		void leave_graph();
		bool apply_param(const std::string& name, float value, bool isTrigger);
		void warn_once(const std::string& key, const std::string& message);
		void run_commands();
		bool resolve(const ClipSelector& selector, const AnimationClipData*& out, bool& unknown) const;
		std::unique_ptr<ClipNode> make_clip(const AnimationClipData& clip, const PlayOptions& options);
		void apply(const Command& command, const AnimationClipData* clip);
		Layer* find_layer(LayerId id);
		const Layer* find_layer(LayerId id) const;

		AnimationSources m_Sources;
		std::vector<AssetRef> m_Clips;
		AssetRef m_SkeletonRef;
		const SkeletonData* m_Skeleton = nullptr;

		std::unique_ptr<PoseNode> m_Root;
		ClipNode* m_Current = nullptr; // the newest clip of the base; null after stop()
		std::vector<Layer> m_Layers;
		LayerId m_NextLayer = 1;
		std::deque<Command> m_Commands;

		AssetRef m_GraphRef{};
		AssetRef m_FallbackClip{};
		bool m_FallbackPlay = false;
		CompiledGraph m_Graph{}; // Root has moved into m_Root once the graph plays
		bool m_GraphMode = false;
		bool m_GraphGaveUp = false;
		struct QueuedParam
		{
			std::string Name{};
			float Value = 0.0f;
			bool Trigger = false;
		};
		std::vector<QueuedParam> m_QueuedParams{};
		u32 m_PendingFrames = 0;   // frames the graph has waited for the skeleton, the graph or a clip
		std::set<std::string> m_Warned{};
		std::map<const AnimationClipData*, std::shared_ptr<const ClipBinding>> m_Bindings;

		Pose m_Pose{};
		std::vector<glm::mat4> m_Model{};
		std::vector<JointMatrix> m_Palette{};
		std::vector<JointMatrix> m_PreviousPalette{};
	};
}
