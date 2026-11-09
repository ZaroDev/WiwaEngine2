#pragma once

#include <optional>
#include <string>

#include <assets/asset_data.h>

#include "edit_history.h"

namespace wiwa
{
	// What is selected in the graph: a layer, and in it a state or a transition (-1: none). The tool pane and the canvas share it.
	struct GraphSelection
	{
		int Layer = 0;
		int State = -1;
		int Transition = -1;
	};

	// The document the Graph editor edits: an AnimationGraphData and its history. No ImGui. Every call that changes the graph is one undo step (or part
	// of the gesture going on); a call that is refused (it returns false, and error() says why) changes nothing and makes no step, and neither does a
	// call that would change nothing. After every call every name is unique, non-empty and at most 64 characters, and every parameter, state and
	// layer a condition, transition, blend axis, speed or weight names exists with a kind that fits. A state may still lack its clip while the graph is
	// being made: graph_valid, which Save enforces, is what asks for it. Renaming a parameter or a state rewrites every reference to it.
	class GraphEditModel
	{
	public:
		static constexpr size_t MAX_NAME_LENGTH = 64;

		explicit GraphEditModel(AnimationGraphData graph);

		const AnimationGraphData& data() const { return m_Graph; }
		const std::string& error() const { return m_Error; }
		void clear_error() { m_Error.clear(); }

		// The skeleton the graph is made for (its mask joints and the preview use it).
		bool set_skeleton(const AssetRef& skeleton);

		// Parameters.
		bool add_parameter(const std::string& name, GraphParamType type, float defaultValue);
		bool rename_parameter(size_t parameter, const std::string& name);
		bool set_parameter_default(size_t parameter, float value);
		// Refused, with where it is used, while a condition, a blend axis, a speed or a layer weight names it.
		bool delete_parameter(size_t parameter);

		// Layers. The first is the base layer: it cannot be deleted, made another mode or masked. A new layer has one empty state.
		bool add_layer(const std::string& name, GraphLayerMode mode);
		bool rename_layer(size_t layer, const std::string& name);
		bool delete_layer(size_t layer);
		bool set_layer_mode(size_t layer, GraphLayerMode mode);
		// parameter: a float parameter that replaces the weight (empty: none).
		bool set_layer_weight(size_t layer, float weight, const std::string& parameter);
		bool set_layer_mask(size_t layer, const std::string& root, float falloff, std::vector<GraphMaskJoint> joints);
		bool set_default_state(size_t layer, size_t state);

		// States.
		bool add_state(size_t layer, const std::string& name);
		bool rename_state(size_t layer, size_t state, const std::string& name);
		// Takes its transitions with it; refused for the last state of a layer.
		bool delete_state(size_t layer, size_t state);
		// A blend space starts with one sample (the state's clip at 0) and the first float parameter as its X axis (refused when there is none).
		bool set_state_body(size_t layer, size_t state, GraphBodyKind kind);
		bool set_state_clip(size_t layer, size_t state, const AssetRef& clip);
		bool set_state_options(size_t layer, size_t state, bool loop, float speed, const std::string& speedParameter);
		bool set_state_position(size_t layer, size_t state, const glm::vec2& position);
		bool add_sample(size_t layer, size_t state, const AssetRef& clip, float x, float y);
		bool set_sample(size_t layer, size_t state, size_t sample, const AssetRef& clip, float x, float y);
		bool delete_sample(size_t layer, size_t state, size_t sample);
		// y empty: a 1D blend space.
		bool set_blend_axes(size_t layer, size_t state, const std::string& x, const std::string& y);

		// Transitions. from empty: from any state. The order is the order they are tried in.
		bool add_transition(size_t layer, const std::string& from, const std::string& to);
		bool delete_transition(size_t layer, size_t transition);
		bool move_transition(size_t layer, size_t transition, size_t newIndex);
		bool set_transition_ends(size_t layer, size_t transition, const std::string& from, const std::string& to);
		// exitTime below 0: none.
		bool set_transition_options(size_t layer, size_t transition, float duration, float exitTime);
		bool add_condition(size_t layer, size_t transition, const GraphCondition& condition);
		bool set_condition(size_t layer, size_t transition, size_t condition, const GraphCondition& value);
		bool delete_condition(size_t layer, size_t transition, size_t condition);

		// A drag changes the graph many times: between begin_gesture and end_gesture the changes make one undo step.
		void begin_gesture();
		void end_gesture();

		void undo();
		void redo();
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		bool dirty() const { return m_History.dirty(); }
		void mark_saved() { m_History.mark_saved(); }

		static std::string trimmed(const std::string& name);

	private:
		bool refuse(const std::string& why);
		void commit(AnimationGraphData edited);
		// Runs change on a copy; an empty return is success, anything else is the reason to refuse. The copy then has to pass the model's rules.
		template<typename F>
		bool apply(F&& change);

		AnimationGraphData m_Graph;
		std::string m_Error{};
		EditHistory<AnimationGraphData> m_History;
		bool m_InGesture = false;
		bool m_GestureChanged = false;
		AnimationGraphData m_GestureBefore{};
	};
}
