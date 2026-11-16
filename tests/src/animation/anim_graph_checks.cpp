#include <editor_common.h>
#include "anim_check.h"

#include <functional>
#include <limits>

#include <animation/graph_compiler.h>
#include <animation/pose.h>
#include <assets/native_anim_files.h>
#include <assets/wasset_io.h>

#include "anim_fixture.h"
#include <asset_editor/graph_edit_model.h>

namespace wiwa
{
	namespace
	{
		AssetRef ref(u64 id)
		{
			return AssetRef{ AssetGuid{ 0xA11C0000ull + id, id }, id == 9 ? 7u : 0u };
		}

		// Two layers: a base machine (idle, a walk/run blend space, jump) and an override layer on "hand" with one state.
		AnimationGraphData make_graph()
		{
			AnimationGraphData graph;
			graph.Name = "hero";
			graph.Skeleton = ref(1);
			graph.Parameters = { { "speed", GraphParamType::Float, 0.5f }, { "grounded", GraphParamType::Bool, 1.0f }, { "jump", GraphParamType::Trigger, 0.0f }, { "stance", GraphParamType::Int, 2.0f } };

			GraphLayer base;
			base.Name = "Base";
			base.Mode = GraphLayerMode::Base;
			base.DefaultState = "Idle";
			GraphState idle;
			idle.Name = "Idle";
			idle.Clip = ref(2);
			idle.Position = glm::vec2(10.0f, 20.0f);
			GraphState move;
			move.Name = "Move";
			move.Body = GraphBodyKind::Blend;
			move.BlendX = "speed";
			move.Samples = { { ref(2), 0.0f, 0.0f }, { ref(3), 1.0f, 0.0f }, { ref(9), 2.0f, 0.0f } };
			move.Speed = 1.5f;
			move.SpeedParameter = "speed";
			move.Loop = false;
			GraphState jump;
			jump.Name = "Jump";
			jump.Clip = ref(4);
			base.States = { idle, move, jump };
			GraphTransition toMove;
			toMove.From = "Idle";
			toMove.To = "Move";
			toMove.Conditions = { { "speed", GraphCompareOp::Greater, 0.1f } };
			toMove.Duration = 0.25f;
			GraphTransition toJump;
			toJump.To = "Jump";
			toJump.Conditions = { { "jump", GraphCompareOp::Fired, 0.0f }, { "grounded", GraphCompareOp::IsTrue, 0.0f } };
			toJump.ExitTime = 0.8f;
			base.Transitions = { toMove, toJump };

			GraphLayer upper;
			upper.Name = "Upper";
			upper.Mode = GraphLayerMode::Override;
			upper.Weight = 0.5f;
			upper.WeightParameter = "speed";
			upper.MaskRoot = "hand";
			upper.MaskFalloff = 0.25f;
			upper.MaskJoints = { { "hip", 0.5f } };
			upper.DefaultState = "Wave";
			GraphState wave;
			wave.Name = "Wave";
			wave.Clip = ref(4);
			upper.States = { wave };
			graph.Layers = { base, upper };
			return graph;
		}

		bool same(const AnimationGraphData& a, const AnimationGraphData& b)
		{
			// the JSON is the one description, so equal text means equal data
			return write_graph_file(a) == write_graph_file(b);
		}

		void graph_file_round_trip(AnimCheck& check)
		{
			const AnimationGraphData graph = make_graph();
			const std::string text = write_graph_file(graph);
			std::string error;
			const std::optional<AnimationGraphData> read = read_graph_file(text, error);
			check(read.has_value(), "graph_file_round_trip: the written graph reads back (" + error + ")");
			if (!read)
			{
				return;
			}
			check(same(graph, *read), "graph_file_round_trip: the text is the same after a round trip");
			check(read->Layers.size() == 2 && read->Parameters.size() == 4, "graph_file_round_trip: layers and parameters kept");
			check(read->Layers[0].States[1].Samples.size() == 3 && read->Layers[0].States[1].Samples[2].Clip == ref(9), "graph_file_round_trip: samples and a sub-asset reference kept");
			check(read->Layers[0].Transitions[1].ExitTime == 0.8f && read->Layers[0].Transitions[0].ExitTime < 0.0f, "graph_file_round_trip: exit time kept, absent stays none");
			check(read->Layers[0].Transitions[1].From.empty() && read->Layers[0].Transitions[1].Conditions[0].Op == GraphCompareOp::Fired, "graph_file_round_trip: Any source and ops kept");
			check(read->Layers[1].MaskRoot == "hand" && read->Layers[1].MaskJoints.size() == 1 && read->Layers[1].WeightParameter == "speed", "graph_file_round_trip: mask and weight parameter kept");
			check(read->Layers[0].States[0].Position == glm::vec2(10.0f, 20.0f), "graph_file_round_trip: canvas position kept");
		}

		// One broken graph per rule: it has to be refused, and the message has to name what is wrong.
		void graph_valid_rejects(AnimCheck& check)
		{
			struct Case
			{
				const char* Name;
				std::function<void(AnimationGraphData&)> Break;
				const char* Mentions;
			};
			const Case cases[] = {
				{ "duplicate parameter", [](AnimationGraphData& g) { g.Parameters.push_back(g.Parameters[0]); }, "speed" },
				{ "empty parameter name", [](AnimationGraphData& g) { g.Parameters[0].Name.clear(); }, "parameter name" },
				{ "no layers", [](AnimationGraphData& g) { g.Layers.clear(); }, "no layers" },
				{ "duplicate layer", [](AnimationGraphData& g) { g.Layers[1].Name = "Base"; }, "Base" },
				{ "first layer not base", [](AnimationGraphData& g) { g.Layers[0].Mode = GraphLayerMode::Override; }, "base" },
				{ "later layer base", [](AnimationGraphData& g) { g.Layers[1].Mode = GraphLayerMode::Base; }, "Upper" },
				{ "base layer mask", [](AnimationGraphData& g) { g.Layers[0].MaskRoot = "hip"; }, "mask" },
				{ "duplicate state", [](AnimationGraphData& g) { g.Layers[0].States[1].Name = "Idle"; }, "Idle" },
				{ "no states", [](AnimationGraphData& g) { g.Layers[1].States.clear(); }, "no states" },
				{ "missing default", [](AnimationGraphData& g) { g.Layers[0].DefaultState = "Nope"; }, "Nope" },
				{ "unknown transition target", [](AnimationGraphData& g) { g.Layers[0].Transitions[0].To = "Nope"; }, "Nope" },
				{ "unknown condition parameter", [](AnimationGraphData& g) { g.Layers[0].Transitions[0].Conditions[0].Parameter = "ghost"; }, "ghost" },
				{ "op does not fit type", [](AnimationGraphData& g) { g.Layers[0].Transitions[0].Conditions[0].Op = GraphCompareOp::Fired; }, "operator" },
				{ "blend axis not a float", [](AnimationGraphData& g) { g.Layers[0].States[1].BlendX = "grounded"; }, "float" },
				{ "blend without samples", [](AnimationGraphData& g) { g.Layers[0].States[1].Samples.clear(); }, "samples" },
				{ "clip state without clip", [](AnimationGraphData& g) { g.Layers[0].States[0].Clip = AssetRef{}; }, "no clip" },
				{ "exit time above 1", [](AnimationGraphData& g) { g.Layers[0].Transitions[1].ExitTime = 1.5f; }, "exit time" },
				{ "NaN speed", [](AnimationGraphData& g) { g.Layers[0].States[0].Speed = std::numeric_limits<float>::quiet_NaN(); }, "speed" },
				{ "mask weight out of range", [](AnimationGraphData& g) { g.Layers[1].MaskJoints[0].Weight = 2.0f; }, "mask joint" },
				{ "weight parameter not a float", [](AnimationGraphData& g) { g.Layers[1].WeightParameter = "jump"; }, "float" },
			};
			for (const Case& c : cases)
			{
				AnimationGraphData graph = make_graph();
				c.Break(graph);
				std::string error;
				const bool valid = graph_valid(graph, error);
				check(!valid, std::string("graph_valid_rejects: ") + c.Name + " is refused");
				check(valid || error.find(c.Mentions) != std::string::npos, std::string("graph_valid_rejects: ") + c.Name + " message mentions '" + c.Mentions + "' (got: " + error + ")");
			}
			std::string error;
			check(graph_valid(make_graph(), error), "graph_valid_rejects: the good graph is valid (" + error + ")");
			const std::optional<AnimationGraphData> bad = read_graph_file("{\"version\":1,\"layers\":[]}", error);
			check(!bad.has_value() && !error.empty(), "graph_valid_rejects: a file without layers fails to read with a reason");
			check(!read_graph_file("not json", error).has_value(), "graph_valid_rejects: text that is not JSON is refused");
		}

		void graph_cooked_round_trip(AnimCheck& check)
		{
			AnimationGraphData graph = make_graph();
			BinaryArchive writer = BinaryArchive::writer();
			archive(writer, graph);
			const std::vector<std::byte> bytes = writer.take();
			BinaryArchive reader = BinaryArchive::reader(bytes);
			AnimationGraphData loaded;
			archive(reader, loaded);
			check(reader.ok() && same(graph, loaded), "graph_cooked_round_trip: the cooked asset reads back the same");

			std::vector<std::byte> corrupt = bytes;
			corrupt[0] = std::byte{ 99 }; // an unknown version
			BinaryArchive bad = BinaryArchive::reader(corrupt);
			AnimationGraphData ignored;
			archive(bad, ignored);
			check(!bad.ok(), "graph_cooked_round_trip: an unknown version is refused");
			check(AssetType::AnimationGraph < AssetType::Last && asset_type_of<AnimationGraphData>() == AssetType::AnimationGraph, "graph_cooked_round_trip: the asset type is registered");
		}

		// ---- the evaluator, on the fixture rig: hip (x of Idle 1, Run 0..10 in a second with a footstep at 0.5, Late 7), hand (Wave 5)
		bool close(float a, float b, float epsilon = 1e-3f)
		{
			return std::abs(a - b) < epsilon;
		}

		GraphState clip_state(const std::string& name, const AssetRef& clip, bool loop = true)
		{
			GraphState state;
			state.Name = name;
			state.Clip = clip;
			state.Loop = loop;
			return state;
		}

		GraphTransition transition(const std::string& from, const std::string& to, std::vector<GraphCondition> conditions, float duration, float exitTime = -1.0f)
		{
			GraphTransition result;
			result.From = from;
			result.To = to;
			result.Conditions = std::move(conditions);
			result.Duration = duration;
			result.ExitTime = exitTime;
			return result;
		}

		struct Rig
		{
			Fixture Fx{};
			AnimationGraphData Graph{};
			CompiledGraph Compiled{};
			std::map<const AnimationClipData*, std::shared_ptr<const ClipBinding>> Bindings{};
			std::vector<ClipEvent> Events{};

			Rig()
			{
				Fx.LateLoaded = true;
				Graph.Skeleton = Fx.SkeletonRef;
				Graph.Parameters = { { "speed", GraphParamType::Float, 0.0f }, { "go", GraphParamType::Bool, 0.0f }, { "jump", GraphParamType::Trigger, 0.0f },
					{ "noop", GraphParamType::Trigger, 0.0f }, { "w", GraphParamType::Float, 0.0f } };
				GraphLayer base;
				base.Name = "Base";
				base.Mode = GraphLayerMode::Base;
				Graph.Layers.push_back(base);
			}

			GraphLayer& base() { return Graph.Layers[0]; }

			bool build()
			{
				Compiled = compile_graph(Graph, Fx.Skeleton, [this](const AssetRef& r) { return Fx.Sources.Clip(r); }, Bindings);
				return Compiled.Status == CompileStatus::Ready;
			}

			void set(const std::string& name, float value) { Compiled.Params->set(Compiled.Params->index_of(name), value); }
			void fire(const std::string& name) { Compiled.Params->fire(Compiled.Params->index_of(name)); }
			bool fired(const std::string& name) { return Compiled.Params->fired(Compiled.Params->index_of(name)); }

			// what AnimatorInstance does each frame
			void step(float dt)
			{
				for (const auto& update : Compiled.WeightUpdates)
				{
					update();
				}
				Compiled.Root->advance(dt, 1.0f, Events);
				Compiled.Params->end_frame();
			}

			Pose pose() const
			{
				Pose result = rest_pose(Fx.Skeleton);
				Compiled.Root->evaluate(result);
				return result;
			}

			float hip() const { return pose().T[0].x; }
			float hand() const { return pose().T[1].x; }
			const std::string& state(size_t layer = 0) const { return Compiled.Machines[layer]->active_state(); }
		};

		void sm_param_transition_frame(AnimCheck& check)
		{
			Rig rig;
			rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef) };
			rig.base().DefaultState = "Idle";
			rig.base().Transitions = { transition("Idle", "Run", { { "speed", GraphCompareOp::Greater, 0.5f } }, 0.5f) };
			check(rig.build(), "sm_param_transition_frame: the graph compiles");
			if (rig.Compiled.Status != CompileStatus::Ready)
			{
				return;
			}
			rig.set("speed", 0.4f);
			rig.step(0.1f);
			check(rig.state() == "Idle" && close(rig.hip(), 1.0f), "sm_param_transition_frame: stays in Idle while speed is 0.4");
			rig.set("speed", 0.6f);
			rig.step(0.1f);
			check(rig.state() == "Run" && rig.Compiled.Machines[0]->fading(), "sm_param_transition_frame: switches on the frame speed passes 0.5");
			check(close(rig.hip(), 1.0f), "sm_param_transition_frame: the pose is still Idle's at the start of the fade");
			rig.step(0.5f);
			check(!rig.Compiled.Machines[0]->fading() && close(rig.hip(), 5.0f), "sm_param_transition_frame: after the fade the pose is Run's (x = 5 at 0.5 s)");
		}

		void sm_exit_time_waits(AnimCheck& check)
		{
			Rig rig;
			rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef) };
			rig.base().DefaultState = "Idle";
			rig.base().Transitions = { transition("Idle", "Run", {}, 0.0f, 0.8f) };
			check(rig.build(), "sm_exit_time_waits: the graph compiles");
			if (rig.Compiled.Status != CompileStatus::Ready)
			{
				return;
			}
			rig.step(0.5f);
			check(rig.state() == "Idle", "sm_exit_time_waits: Idle at 50% of its length");
			rig.step(0.2f);
			check(rig.state() == "Idle", "sm_exit_time_waits: Idle at 70%");
			rig.step(0.2f);
			check(rig.state() == "Run", "sm_exit_time_waits: Run once 80% is passed");
		}

		void sm_interrupt_continuous(AnimCheck& check)
		{
			Rig rig;
			rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef), clip_state("Late", rig.Fx.LateRef) };
			rig.base().DefaultState = "Idle";
			rig.base().Transitions = { transition("Idle", "Run", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 1.0f), transition("Run", "Late", { { "jump", GraphCompareOp::Fired, 0.0f } }, 1.0f) };
			check(rig.build(), "sm_interrupt_continuous: the graph compiles");
			if (rig.Compiled.Status != CompileStatus::Ready)
			{
				return;
			}
			rig.set("go", 1.0f);
			rig.step(0.1f);
			rig.step(0.5f); // Run at 0.5 s (x = 5) over Idle (x = 1), halfway: x = 3
			const float before = rig.hip();
			check(close(before, 3.0f), "sm_interrupt_continuous: halfway through the first fade the pose is 3");
			rig.fire("jump");
			rig.step(0.0f);
			check(close(rig.hip(), before), "sm_interrupt_continuous: the pose does not jump when a second transition fires mid-fade");
			rig.step(1.0f);
			check(rig.state() == "Late" && close(rig.hip(), 7.0f), "sm_interrupt_continuous: the second fade ends in Late");
		}

		void sm_trigger_consumed_once(AnimCheck& check)
		{
			Rig rig;
			rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef), clip_state("Late", rig.Fx.LateRef) };
			rig.base().DefaultState = "Idle";
			rig.base().Transitions = { transition("Idle", "Run", { { "jump", GraphCompareOp::Fired, 0.0f } }, 0.0f), transition("Run", "Late", { { "jump", GraphCompareOp::Fired, 0.0f } }, 0.0f) };
			check(rig.build(), "sm_trigger_consumed_once: the graph compiles");
			if (rig.Compiled.Status != CompileStatus::Ready)
			{
				return;
			}
			rig.fire("jump");
			rig.step(0.1f);
			check(rig.state() == "Run", "sm_trigger_consumed_once: the trigger takes the first transition");
			rig.step(0.1f);
			check(rig.state() == "Run", "sm_trigger_consumed_once: the same trigger does not take the next one");
			rig.fire("noop");
			rig.step(0.1f);
			check(!rig.fired("noop"), "sm_trigger_consumed_once: an unused trigger is cleared at the end of the frame");
			rig.fire("jump");
			rig.step(0.1f);
			check(rig.state() == "Late", "sm_trigger_consumed_once: a new trigger fires again");
			rig.fire("jump");
			rig.step(0.1f);
			check(!rig.fired("jump") && rig.state() == "Late", "sm_trigger_consumed_once: a trigger with no transition from the state is cleared");
		}

		void sm_any_state_and_ties(AnimCheck& check)
		{
			{
				Rig rig;
				rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef), clip_state("Late", rig.Fx.LateRef) };
				rig.base().DefaultState = "Idle";
				rig.base().Transitions = { transition("Idle", "Run", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 0.0f), transition("", "Late", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 0.0f) };
				check(rig.build(), "sm_any_state_and_ties: the graph compiles");
				if (rig.Compiled.Status != CompileStatus::Ready)
				{
					return;
				}
				rig.set("go", 1.0f);
				rig.step(0.1f);
				check(rig.state() == "Run", "sm_any_state_and_ties: both are ready, the one from the active state wins");
				rig.step(0.1f);
				check(rig.state() == "Late", "sm_any_state_and_ties: the Any transition fires from Run");
				rig.step(0.1f);
				check(rig.state() == "Late", "sm_any_state_and_ties: a transition to the active state is ignored");
			}
			{
				Rig rig;
				rig.base().States = { clip_state("Idle", rig.Fx.IdleRef), clip_state("Run", rig.Fx.RunRef), clip_state("Late", rig.Fx.LateRef) };
				rig.base().DefaultState = "Idle";
				rig.base().Transitions = { transition("Idle", "Run", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 0.0f), transition("Idle", "Late", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 0.0f) };
				check(rig.build(), "sm_any_state_and_ties: the second graph compiles");
				rig.set("go", 1.0f);
				rig.step(0.1f);
				check(rig.state() == "Run", "sm_any_state_and_ties: two ready transitions, the first in file order wins");
			}
		}

		struct SpaceRig
		{
			Fixture Fx{};
			GraphParams Params;
			std::map<const AnimationClipData*, std::shared_ptr<const ClipBinding>> Bindings{};
			std::unique_ptr<BlendSpaceNode> Node;

			explicit SpaceRig(const std::vector<glm::vec2>& points, bool is2D, const std::vector<AssetRef>& clips = {}) : Params({ { "x", GraphParamType::Float, 0.0f }, { "y", GraphParamType::Float, 0.0f } })
			{
				Fx.LateLoaded = true;
				std::vector<AssetRef> refs = clips.empty() ? std::vector<AssetRef>{ Fx.IdleRef, Fx.RunRef, Fx.LateRef } : clips;
				std::vector<std::unique_ptr<ClipNode>> samples;
				for (size_t i = 0; i < points.size(); i++)
				{
					const AnimationClipData* clip = Fx.Sources.Clip(refs[i % refs.size()]);
					auto& binding = Bindings[clip];
					if (!binding)
					{
						binding = std::make_shared<const ClipBinding>(bind_clip(*clip, Fx.Skeleton));
					}
					samples.push_back(std::make_unique<ClipNode>(*clip, Fx.Skeleton, binding));
				}
				Node = std::make_unique<BlendSpaceNode>(std::move(samples), points, true, Params, 0, is2D ? 1 : -1);
			}

			std::vector<float> weights(float x, float y = 0.0f)
			{
				Params.set(0, x);
				Params.set(1, y);
				return Node->sample_weights();
			}
		};

		bool weights_are(const std::vector<float>& got, std::vector<float> want)
		{
			if (got.size() != want.size())
			{
				return false;
			}
			for (size_t i = 0; i < got.size(); i++)
			{
				if (!close(got[i], want[i]))
				{
					return false;
				}
			}
			return true;
		}

		void blend_space_1d_clamps_and_weights(AnimCheck& check)
		{
			SpaceRig rig({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f } }, false);
			check(weights_are(rig.weights(-1.0f), { 1, 0, 0 }), "blend_space_1d: below the range is the first sample");
			check(weights_are(rig.weights(0.5f), { 0.5f, 0.5f, 0 }), "blend_space_1d: halfway between the first two");
			check(weights_are(rig.weights(1.0f), { 0, 1, 0 }), "blend_space_1d: on a sample is that sample");
			check(weights_are(rig.weights(1.5f), { 0, 0.5f, 0.5f }), "blend_space_1d: halfway between the last two");
			check(weights_are(rig.weights(5.0f), { 0, 0, 1 }), "blend_space_1d: above the range is the last sample");
			Pose pose = rest_pose(rig.Fx.Skeleton);
			rig.weights(0.5f);
			rig.Node->evaluate(pose);
			check(close(pose.T[0].x, 0.5f), "blend_space_1d: the pose is the weighted blend (Idle 1, Run 0 at the start: 0.5)");
		}

		void blend_space_2d_at_sample_and_between(AnimCheck& check)
		{
			SpaceRig rig({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }, true);
			check(weights_are(rig.weights(1.0f, 0.0f), { 0, 1, 0 }), "blend_space_2d: on a sample is that sample");
			check(weights_are(rig.weights(0.25f, 0.25f), { 0.6f, 0.2f, 0.2f }), "blend_space_2d: between three samples the nearest one weighs most (gradient bands: 0.6, 0.2, 0.2)");
			const std::vector<float> outside = rig.weights(2.0f, 2.0f);
			float sum = 0.0f;
			bool nonNegative = true;
			for (const float w : outside)
			{
				sum += w;
				nonNegative = nonNegative && w >= 0.0f;
			}
			check(close(sum, 1.0f) && nonNegative, "blend_space_2d: outside the samples the weights still sum to 1 and are not negative");
		}

		// the reviewer's case: five samples in a cross with one in the middle. The weights must not jump when the point moves a little, wherever it is.
		void blend_space_2d_is_continuous(AnimCheck& check)
		{
			SpaceRig rig({ { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { -1.0f, 0.0f }, { 0.0f, 0.0f } }, true);
			check(weights_are(rig.weights(0.0f, 0.0f), { 0, 0, 0, 0, 1 }), "blend_space_2d_is_continuous: the middle sample at its own point");
			float worst = 0.0f;
			for (float x = -1.5f; x <= 1.5f; x += 0.05f)
			{
				for (float y = -1.5f; y <= 1.5f; y += 0.05f)
				{
					const std::vector<float> a = rig.weights(x, y);
					const std::vector<float> b = rig.weights(x + 0.01f, y + 0.01f);
					float sum = 0.0f;
					for (size_t i = 0; i < a.size(); i++)
					{
						worst = std::max(worst, std::abs(a[i] - b[i]));
						sum += a[i];
					}
					check(close(sum, 1.0f), "blend_space_2d_is_continuous: the weights sum to 1");
					if (!close(sum, 1.0f))
					{
						return;
					}
				}
			}
			check(worst < 0.15f, "blend_space_2d_is_continuous: moving the point by 0.014 never changes a weight by more than 0.15 (largest " + std::to_string(worst) + ")");
			SpaceRig triangle({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }, true);
			const std::vector<float> inside = triangle.weights(0.5f, 0.001f);
			const std::vector<float> outside = triangle.weights(0.5f, -0.001f);
			check(std::abs(inside[2] - outside[2]) < 0.05f, "blend_space_2d_is_continuous: crossing the edge of the samples does not make a sample pop in");
		}

		void blend_space_degenerate(AnimCheck& check)
		{
			SpaceRig one({ { 0.0f, 0.0f } }, false);
			check(weights_are(one.weights(3.0f), { 1 }), "blend_space_degenerate: one sample is always that sample");
			SpaceRig twin({ { 1.0f, 0.0f }, { 1.0f, 0.0f } }, false);
			check(weights_are(twin.weights(0.0f), { 1, 0 }) && weights_are(twin.weights(1.0f), { 1, 0 }) && weights_are(twin.weights(2.0f), { 1, 0 }), "blend_space_degenerate: two samples at the same x give the first one");
			SpaceRig line({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f } }, true);
			const std::vector<float> w = line.weights(0.5f, 1.0f);
			float sum = 0.0f;
			for (const float v : w)
			{
				sum += v;
			}
			check(close(sum, 1.0f), "blend_space_degenerate: a 2D space of samples on a line (no triangle) still gives weights that sum to 1");
		}

		void layer_mask_applies(AnimCheck& check)
		{
			{
				Rig rig;
				rig.base().States = { clip_state("Idle", rig.Fx.IdleRef) };
				rig.base().DefaultState = "Idle";
				GraphLayer upper;
				upper.Name = "Upper";
				upper.Mode = GraphLayerMode::Override;
				upper.MaskRoot = "hand";
				upper.States = { clip_state("Wave", rig.Fx.WaveRef) };
				upper.DefaultState = "Wave";
				rig.Graph.Layers.push_back(upper);
				check(rig.build(), "layer_mask_applies: the graph compiles");
				if (rig.Compiled.Status != CompileStatus::Ready)
				{
					return;
				}
				rig.step(0.1f);
				check(close(rig.hip(), 1.0f) && close(rig.hand(), 5.0f), "layer_mask_applies: outside the mask the base plays (hip 1), inside the layer does (hand 5)");
			}
			{
				Rig rig;
				rig.base().States = { clip_state("Idle", rig.Fx.IdleRef) };
				rig.base().DefaultState = "Idle";
				GraphLayer upper;
				upper.Name = "Upper";
				upper.Mode = GraphLayerMode::Override;
				upper.MaskRoot = "hip";
				upper.MaskFalloff = 0.5f;
				upper.States = { clip_state("Wave", rig.Fx.WaveRef) };
				upper.DefaultState = "Wave";
				rig.Graph.Layers.push_back(upper);
				rig.build();
				rig.step(0.1f);
				check(close(rig.hand(), 3.0f) && close(rig.hip(), 0.0f), "layer_mask_applies: the weight drops by the falloff per joint of depth (hand: halfway between rest 1 and 5)");
			}
		}

		void additive_layer_delta(AnimCheck& check)
		{
			Rig rig;
			rig.base().States = { clip_state("Idle", rig.Fx.IdleRef) };
			rig.base().DefaultState = "Idle";
			GraphLayer extra;
			extra.Name = "Extra";
			extra.Mode = GraphLayerMode::Additive;
			extra.WeightParameter = "w";
			extra.States = { clip_state("Run", rig.Fx.RunRef) };
			extra.DefaultState = "Run";
			rig.Graph.Layers.push_back(extra);
			check(rig.build(), "additive_layer_delta: the graph compiles");
			if (rig.Compiled.Status != CompileStatus::Ready)
			{
				return;
			}
			rig.step(0.5f);
			check(close(rig.hip(), 1.0f), "additive_layer_delta: a layer at weight 0 (its parameter's default) changes nothing");
			rig.set("w", 0.5f);
			rig.step(0.0f);
			check(close(rig.hip(), 1.0f + 0.5f * 5.0f), "additive_layer_delta: the layer adds the change from its first frame by the parameter's weight (Run moved 5)");
		}

		void event_fires_for_dominant_state_only(AnimCheck& check)
		{
			const auto footsteps = [](float fadeAdvance)
			{
				Rig rig;
				rig.base().States = { clip_state("Run", rig.Fx.RunRef), clip_state("Idle", rig.Fx.IdleRef) };
				rig.base().DefaultState = "Run";
				rig.base().Transitions = { transition("Run", "Idle", { { "go", GraphCompareOp::IsTrue, 0.0f } }, 1.0f) };
				rig.build();
				rig.step(0.1f);
				rig.set("go", 1.0f);
				rig.step(0.0f);
				rig.Events.clear();
				rig.step(fadeAdvance);
				return rig.Events.size();
			};
			check(footsteps(0.45f) == 1, "event_fires_for_dominant_state_only: the outgoing state that is still more than half of the pose fires its event");
			check(footsteps(0.7f) == 0, "event_fires_for_dominant_state_only: an outgoing state under half of the pose fires nothing");
		}

		// ---- AnimatorInstance in graph mode
		Animator graph_config(const Fixture& fx)
		{
			Animator animator = fx.config();
			animator.Graph = fx.GraphRef;
			return animator;
		}

		void instance_graph_plays_from_start(AnimCheck& check)
		{
			Fixture fx;
			fx.add_graph();
			AnimatorInstance instance(fx.Sources, graph_config(fx));
			std::vector<ClipEvent> events;
			instance.update(0.0f, events);
			check(instance.graph_mode() && instance.layer_count() == 1, "instance_graph_plays_from_start: graph mode once everything has loaded");
			check(instance.state_name(0) == "Idle" && close(instance.pose().T[0].x, 1.0f), "instance_graph_plays_from_start: the default state plays");
			instance.trigger("jump");
			instance.update(0.5f, events);
			check(instance.state_name(0) == "Run" && close(instance.pose().T[0].x, 0.0f), "instance_graph_plays_from_start: a trigger takes the transition (Run starts at its first frame)");
		}

		void instance_graph_pending_then_ready(AnimCheck& check)
		{
			Fixture fx;
			fx.add_graph();
			fx.Graphs[fx.GraphRef.Guid].Layers[0].States[0].Clip = fx.LateRef; // the default state needs a clip that has not loaded
			AnimatorInstance instance(fx.Sources, graph_config(fx));
			std::vector<ClipEvent> events;
			check(instance.set_param("speed", 3.0f), "instance_graph_pending_then_ready: a parameter write before the graph starts is accepted");
			instance.update(0.1f, events);
			check(!instance.graph_mode(), "instance_graph_pending_then_ready: waits for the clip");
			fx.LateLoaded = true;
			instance.update(0.1f, events);
			check(instance.graph_mode() && close(instance.pose().T[0].x, 7.0f), "instance_graph_pending_then_ready: starts once the clip has loaded");
			check(instance.param("speed").value_or(-1.0f) == 3.0f, "instance_graph_pending_then_ready: the queued write was applied");
		}

		void instance_graph_wrong_skeleton_falls_back(AnimCheck& check)
		{
			Fixture fx;
			fx.add_graph();
			SkeletonData other = fx.Skeleton;
			other.JointNames = { "pelvis", "wrist" };
			const AssetRef otherRef = AssetRef{ AssetGuid{ 0xA11C0000ull + 99, 99 }, 0 };
			fx.Graphs[fx.GraphRef.Guid].Skeleton = otherRef;
			AnimationSources sources = fx.Sources;
			sources.Skeleton = [&](const AssetRef& ref) -> const SkeletonData* { return ref == otherRef ? &other : fx.Sources.Skeleton(ref); };
			AnimatorInstance instance(sources, graph_config(fx));
			std::vector<ClipEvent> events;
			instance.update(0.1f, events);
			check(!instance.graph_mode(), "instance_graph_wrong_skeleton_falls_back: a graph for another skeleton is not played");
			instance.update(0.1f, events);
			check(instance.playing() && close(instance.pose().T[0].x, 1.0f), "instance_graph_wrong_skeleton_falls_back: the Animator's Clip plays instead");
			check(!instance.set_param("speed", 1.0f), "instance_graph_wrong_skeleton_falls_back: parameter writes are refused afterwards");
		}

		void instance_param_api(AnimCheck& check)
		{
			Fixture fx;
			fx.add_graph();
			AnimatorInstance instance(fx.Sources, graph_config(fx));
			std::vector<ClipEvent> events;
			instance.update(0.0f, events);
			check(!instance.set_param("ghost", 1.0f) && !instance.trigger("ghost"), "instance_param_api: an unknown name is refused");
			check(!instance.set_param("jump", 1.0f), "instance_param_api: a value for a trigger is refused");
			check(!instance.trigger("speed"), "instance_param_api: a trigger call on a float is refused");
			check(!instance.set_param("speed", std::numeric_limits<float>::quiet_NaN()) && !instance.set_param("speed", std::numeric_limits<float>::infinity()), "instance_param_api: a value that is not a number is refused");
			check(instance.param("speed").value_or(-1.0f) == 0.0f, "instance_param_api: the refused writes changed nothing");
			check(instance.set_param("go", 5.0f) && instance.param("go").value_or(-1.0f) == 1.0f, "instance_param_api: a bool takes any non-zero as true");
			check(instance.set_param("speed", 2.0f) && instance.param("speed").value_or(-1.0f) == 2.0f, "instance_param_api: a float is stored");
			check(instance.state_name(0) == "Idle" && instance.state_name(7).empty(), "instance_param_api: the state of a layer, empty for a layer that is not there");
		}

		void instance_play_leaves_graph_mode(AnimCheck& check)
		{
			Fixture fx;
			fx.add_graph();
			AnimatorInstance instance(fx.Sources, graph_config(fx));
			std::vector<ClipEvent> events;
			instance.update(0.0f, events);
			check(instance.graph_mode(), "instance_play_leaves_graph_mode: starts in graph mode");
			instance.play(ClipSelector{ "Run", {} });
			instance.update(0.5f, events);
			check(!instance.graph_mode() && close(instance.pose().T[0].x, 5.0f), "instance_play_leaves_graph_mode: play takes the base pose over");
			instance.update(0.1f, events);
			check(!instance.graph_mode(), "instance_play_leaves_graph_mode: and the graph does not come back");
			check(!instance.set_param("speed", 1.0f), "instance_play_leaves_graph_mode: parameter writes are refused afterwards");
		}

		// ---- GraphEditModel (make_graph(): speed, grounded, jump, stance; layer Base with Idle, Move, Jump; layer Upper with Wave)
		bool contains(const std::string& text, const std::string& part)
		{
			return text.find(part) != std::string::npos;
		}

		void graph_model_rename_parameter_propagates(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(model.rename_parameter(0, "velocity"), "graph_model_rename_parameter_propagates: the rename is accepted (" + model.error() + ")");
			const AnimationGraphData& g = model.data();
			check(g.Parameters[0].Name == "velocity", "graph_model_rename_parameter_propagates: the parameter has the new name");
			check(g.Layers[0].States[1].SpeedParameter == "velocity" && g.Layers[0].States[1].BlendX == "velocity", "graph_model_rename_parameter_propagates: the speed and the blend axis follow");
			check(g.Layers[0].Transitions[0].Conditions[0].Parameter == "velocity", "graph_model_rename_parameter_propagates: the condition follows");
			check(g.Layers[1].WeightParameter == "velocity", "graph_model_rename_parameter_propagates: the layer weight follows");
			std::string error;
			check(graph_valid(g, error), "graph_model_rename_parameter_propagates: the graph is still valid (" + error + ")");
			model.undo();
			check(model.data().Parameters[0].Name == "speed" && model.data().Layers[0].Transitions[0].Conditions[0].Parameter == "speed" && !model.can_undo(), "graph_model_rename_parameter_propagates: one undo puts every name back");
			check(!model.rename_parameter(0, "grounded") && !model.rename_parameter(0, "   "), "graph_model_rename_parameter_propagates: a taken or empty name is refused");
		}

		void graph_model_delete_used_parameter_refused_lists_users(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			const std::string before = write_graph_file(model.data());
			check(!model.delete_parameter(0), "graph_model_delete_used_parameter_refused_lists_users: a used parameter cannot be deleted");
			check(contains(model.error(), "speed") && contains(model.error(), "weight of layer 'Upper'") && contains(model.error(), "blend axis") && contains(model.error(), "condition"),
				"graph_model_delete_used_parameter_refused_lists_users: the refusal lists where it is used (got: " + model.error() + ")");
			check(write_graph_file(model.data()) == before && !model.can_undo(), "graph_model_delete_used_parameter_refused_lists_users: nothing changed, no step");
			check(model.delete_parameter(3) && model.data().Parameters.size() == 3, "graph_model_delete_used_parameter_refused_lists_users: an unused parameter goes");
		}

		void graph_model_rename_state_rewrites_transitions_and_default(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(model.rename_state(0, 0, "Rest"), "graph_model_rename_state: accepted (" + model.error() + ")");
			const GraphLayer& base = model.data().Layers[0];
			check(base.DefaultState == "Rest" && base.Transitions[0].From == "Rest", "graph_model_rename_state: the default and the transition's source follow");
			check(base.Transitions[1].From.empty() && base.Transitions[1].To == "Jump", "graph_model_rename_state: other transitions are left alone");
			check(!model.rename_state(0, 1, "Rest") && !model.rename_state(0, 1, ""), "graph_model_rename_state: a taken or empty name is refused");
		}

		void graph_model_delete_state_removes_transitions(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(model.delete_state(0, 1), "graph_model_delete_state: Move can be deleted (" + model.error() + ")");
			check(model.data().Layers[0].States.size() == 2 && model.data().Layers[0].Transitions.size() == 1 && model.data().Layers[0].Transitions[0].To == "Jump", "graph_model_delete_state: its transitions go with it");
			check(model.delete_state(0, 0), "graph_model_delete_state: the default state can be deleted");
			check(model.data().Layers[0].DefaultState == "Jump", "graph_model_delete_state: the default becomes the first state left");
		}

		void graph_model_delete_last_state_and_base_layer_refused(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(!model.delete_state(1, 0), "graph_model_last: the last state of a layer cannot be deleted");
			check(!model.delete_layer(0) && !model.set_layer_mode(0, GraphLayerMode::Override) && !model.add_layer("Another", GraphLayerMode::Base), "graph_model_last: the base layer stays the base layer");
			check(!model.set_layer_mode(1, GraphLayerMode::Base), "graph_model_last: no other layer becomes a base layer");
			check(!model.can_undo(), "graph_model_last: refusals make no steps");
			check(model.add_layer("Third", GraphLayerMode::Additive) && model.data().Layers.size() == 3 && model.data().Layers[2].States.size() == 1, "graph_model_last: a new layer has one state");
			check(model.delete_layer(2), "graph_model_last: a layer that is not the base can be deleted");
		}

		void graph_model_undo_to_saved_is_clean(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			model.mark_saved();
			check(!model.dirty(), "graph_model_undo_to_saved_is_clean: clean after save");
			model.add_state(0, "Extra");
			check(model.dirty(), "graph_model_undo_to_saved_is_clean: an edit makes it dirty");
			model.undo();
			check(!model.dirty(), "graph_model_undo_to_saved_is_clean: undoing back to the saved state is clean");
			model.redo();
			check(model.dirty() && model.data().Layers[0].States.size() == 4, "graph_model_undo_to_saved_is_clean: redo is dirty again");
		}

		void graph_model_refused_makes_no_step(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(!model.add_parameter("speed", GraphParamType::Float, 0.0f) && !model.add_parameter("  ", GraphParamType::Float, 0.0f), "graph_model_refused_makes_no_step: a taken or empty parameter name is refused");
			check(!model.set_parameter_default(0, std::numeric_limits<float>::quiet_NaN()) && !model.set_state_position(0, 0, glm::vec2(std::numeric_limits<float>::infinity(), 0.0f)), "graph_model_refused_makes_no_step: values that are not numbers are refused");
			check(!model.delete_state(9, 0) && !model.delete_transition(0, 9) && !model.set_sample(0, 1, 9, ref(2), 0.0f, 0.0f), "graph_model_refused_makes_no_step: indices that are not there are refused");
			check(!model.can_undo(), "graph_model_refused_makes_no_step: none of them made a step");
			check(model.set_state_position(0, 0, glm::vec2(10.0f, 20.0f)) && !model.can_undo(), "graph_model_refused_makes_no_step: setting a value to what it is makes no step");
		}

		void graph_model_gesture_one_step(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			model.begin_gesture();
			for (int i = 1; i <= 5; i++)
			{
				model.set_state_position(0, 0, glm::vec2(10.0f + static_cast<float>(i) * 7.0f, 20.0f));
			}
			model.end_gesture();
			check(model.data().Layers[0].States[0].Position.x == 45.0f, "graph_model_gesture_one_step: the drag moved the state");
			model.undo();
			check(model.data().Layers[0].States[0].Position == glm::vec2(10.0f, 20.0f) && !model.can_undo(), "graph_model_gesture_one_step: one undo takes the whole drag back");
		}

		void graph_model_edits_a_broken_graph(AnimCheck& check)
		{
			AnimationGraphData broken = make_graph();
			broken.Layers[0].Transitions[0].Conditions[0].Parameter = "ghost"; // a defect...
			broken.Layers[1].WeightParameter = "jump";                          // ...and a second one (a trigger cannot be a weight)
			GraphEditModel model(broken);
			check(model.set_state_position(0, 0, glm::vec2(5.0f, 5.0f)) && model.data().Layers[0].States[0].Position == glm::vec2(5.0f, 5.0f), "graph_model_edits_a_broken_graph: an edit that has nothing to do with the defects goes through (" + model.error() + ")");
			check(model.set_layer_weight(1, 0.5f, "") && model.delete_transition(0, 0), "graph_model_edits_a_broken_graph: and the defects can be repaired one by one (" + model.error() + ")");
			std::string error;
			check(graph_valid(model.data(), error), "graph_model_edits_a_broken_graph: repaired, the graph is valid (" + error + ")");
			check(!model.rename_state(0, 0, "Move"), "graph_model_edits_a_broken_graph: once valid again, an edit that breaks it is refused");
		}

		void graph_model_transition_order(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(model.move_transition(0, 0, 1) && model.data().Layers[0].Transitions[0].To == "Jump" && model.data().Layers[0].Transitions[1].To == "Move", "graph_model_transition_order: moving swaps the order");
			model.undo();
			check(model.data().Layers[0].Transitions[0].To == "Move", "graph_model_transition_order: undo restores it");
			check(!model.move_transition(0, 0, 5), "graph_model_transition_order: a place that is not there is refused");
		}

		void graph_model_conditions_and_blend(AnimCheck& check)
		{
			GraphEditModel model(make_graph());
			check(!model.add_condition(0, 0, { "speed", GraphCompareOp::Fired, 0.0f }), "graph_model_conditions: an operator that does not fit the parameter's kind is refused");
			check(!model.add_condition(0, 0, { "ghost", GraphCompareOp::Greater, 0.0f }), "graph_model_conditions: an unknown parameter is refused");
			check(model.add_condition(0, 0, { "stance", GraphCompareOp::Equal, 2.0f }) && model.data().Layers[0].Transitions[0].Conditions.size() == 2, "graph_model_conditions: a fitting condition is added");
			check(model.delete_condition(0, 0, 1) && model.data().Layers[0].Transitions[0].Conditions.size() == 1, "graph_model_conditions: and deleted");

			AnimationGraphData bare = make_graph();
			bare.Parameters = { { "go", GraphParamType::Bool, 0.0f } };
			bare.Layers.resize(1);
			bare.Layers[0].States.resize(1);
			bare.Layers[0].Transitions.clear();
			bare.Layers[0].States[0].SpeedParameter.clear();
			bare.Layers[0].DefaultState = bare.Layers[0].States[0].Name;
			GraphEditModel noFloat(bare);
			check(!noFloat.set_state_body(0, 0, GraphBodyKind::Blend) && contains(noFloat.error(), "float parameter"), "graph_model_conditions: a blend space needs a float parameter to exist");
			GraphEditModel withFloat(make_graph());
			check(withFloat.set_state_body(0, 0, GraphBodyKind::Blend) && withFloat.data().Layers[0].States[0].Samples.size() == 1 && withFloat.data().Layers[0].States[0].BlendX == "speed", "graph_model_conditions: a new blend space has one sample and the first float parameter as its axis");
			check(withFloat.set_state_body(0, 0, GraphBodyKind::Clip) && withFloat.data().Layers[0].States[0].Samples.empty() && withFloat.data().Layers[0].States[0].Clip.valid(), "graph_model_conditions: back to a clip keeps a clip");
			check(withFloat.set_state_clip(0, 0, AssetRef{}), "graph_model_conditions: a state may lack its clip while the graph is made");
			std::string error;
			check(!graph_valid(withFloat.data(), error), "graph_model_conditions: but graph_valid, which Save enforces, asks for it");
		}
	}

	void run_graph_checks(AnimCheck& check)
	{
		graph_model_rename_parameter_propagates(check);
		graph_model_delete_used_parameter_refused_lists_users(check);
		graph_model_rename_state_rewrites_transitions_and_default(check);
		graph_model_delete_state_removes_transitions(check);
		graph_model_delete_last_state_and_base_layer_refused(check);
		graph_model_undo_to_saved_is_clean(check);
		graph_model_refused_makes_no_step(check);
		graph_model_gesture_one_step(check);
		graph_model_edits_a_broken_graph(check);
		graph_model_transition_order(check);
		graph_model_conditions_and_blend(check);
		instance_graph_plays_from_start(check);
		instance_graph_pending_then_ready(check);
		instance_graph_wrong_skeleton_falls_back(check);
		instance_param_api(check);
		instance_play_leaves_graph_mode(check);
		graph_file_round_trip(check);
		graph_valid_rejects(check);
		graph_cooked_round_trip(check);
		sm_param_transition_frame(check);
		sm_exit_time_waits(check);
		sm_interrupt_continuous(check);
		sm_trigger_consumed_once(check);
		sm_any_state_and_ties(check);
		blend_space_1d_clamps_and_weights(check);
		blend_space_2d_at_sample_and_between(check);
		blend_space_2d_is_continuous(check);
		blend_space_degenerate(check);
		layer_mask_applies(check);
		additive_layer_delta(check);
		event_fires_for_dominant_state_only(check);
	}
}
