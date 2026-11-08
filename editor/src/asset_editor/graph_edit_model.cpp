#include <editor_common.h>
#include "graph_edit_model.h"

#include <cmath>

#include <assets/native_anim_files.h>

namespace wiwa
{
	namespace
	{
		const char* NO_LAYER = "there is no such layer";
		const char* NO_STATE = "there is no such state";
		const char* NO_TRANSITION = "there is no such transition";
		const char* NO_PARAMETER = "there is no such parameter";

		GraphLayer* layer_at(AnimationGraphData& graph, size_t layer)
		{
			return layer < graph.Layers.size() ? &graph.Layers[layer] : nullptr;
		}

		GraphState* state_at(AnimationGraphData& graph, size_t layer, size_t state)
		{
			GraphLayer* l = layer_at(graph, layer);
			return l != nullptr && state < l->States.size() ? &l->States[state] : nullptr;
		}

		GraphTransition* transition_at(AnimationGraphData& graph, size_t layer, size_t transition)
		{
			GraphLayer* l = layer_at(graph, layer);
			return l != nullptr && transition < l->Transitions.size() ? &l->Transitions[transition] : nullptr;
		}

		// Where a parameter is used, for the refusal to delete it.
		std::string where_used(const AnimationGraphData& graph, const std::string& name)
		{
			std::vector<std::string> places;
			for (const GraphLayer& layer : graph.Layers)
			{
				if (layer.WeightParameter == name)
				{
					places.push_back("the weight of layer '" + layer.Name + "'");
				}
				for (const GraphState& state : layer.States)
				{
					if (state.SpeedParameter == name)
					{
						places.push_back("the speed of state '" + state.Name + "' in layer '" + layer.Name + "'");
					}
					if (state.BlendX == name || state.BlendY == name)
					{
						places.push_back("a blend axis of state '" + state.Name + "' in layer '" + layer.Name + "'");
					}
				}
				for (const GraphTransition& transition : layer.Transitions)
				{
					for (const GraphCondition& condition : transition.Conditions)
					{
						if (condition.Parameter == name)
						{
							places.push_back("a condition of the transition " + (transition.From.empty() ? std::string("Any") : transition.From) + " -> " + transition.To + " in layer '" + layer.Name + "'");
						}
					}
				}
			}
			std::string text;
			for (size_t i = 0; i < places.size() && i < 5; i++)
			{
				text += (i == 0 ? "" : ", ") + places[i];
			}
			if (places.size() > 5)
			{
				text += " and " + std::to_string(places.size() - 5) + " more";
			}
			return text;
		}

		void rewrite_parameter(AnimationGraphData& graph, const std::string& from, const std::string& to)
		{
			for (GraphLayer& layer : graph.Layers)
			{
				if (layer.WeightParameter == from) { layer.WeightParameter = to; }
				for (GraphState& state : layer.States)
				{
					if (state.SpeedParameter == from) { state.SpeedParameter = to; }
					if (state.BlendX == from) { state.BlendX = to; }
					if (state.BlendY == from) { state.BlendY = to; }
				}
				for (GraphTransition& transition : layer.Transitions)
				{
					for (GraphCondition& condition : transition.Conditions)
					{
						if (condition.Parameter == from) { condition.Parameter = to; }
					}
				}
			}
		}

		// The rules of graph_valid except that a state may still lack its clip (a new state, a new sample): those get a stand-in for the check.
		bool model_valid(const AnimationGraphData& graph, std::string& problem)
		{
			AnimationGraphData copy = graph;
			const AssetRef stand_in{ AssetGuid{ 1, 1 }, 0 };
			for (GraphLayer& layer : copy.Layers)
			{
				for (GraphState& state : layer.States)
				{
					if (state.Body == GraphBodyKind::Clip && !state.Clip.valid())
					{
						state.Clip = stand_in;
					}
					for (GraphBlendSample& sample : state.Samples)
					{
						if (!sample.Clip.valid())
						{
							sample.Clip = stand_in;
						}
					}
				}
			}
			return graph_valid(copy, problem);
		}

		size_t graph_bytes(const AnimationGraphData& graph)
		{
			return write_graph_file(graph).size();
		}

		bool finite(float value)
		{
			return std::isfinite(value);
		}
	}

	GraphEditModel::GraphEditModel(AnimationGraphData graph) : m_Graph(std::move(graph)) {}

	std::string GraphEditModel::trimmed(const std::string& name)
	{
		const size_t begin = name.find_first_not_of(" \t");
		if (begin == std::string::npos)
		{
			return {};
		}
		return name.substr(begin, name.find_last_not_of(" \t") - begin + 1);
	}

	bool GraphEditModel::refuse(const std::string& why)
	{
		m_Error = why;
		return false;
	}

	void GraphEditModel::commit(AnimationGraphData edited)
	{
		if (m_InGesture)
		{
			m_GestureChanged = true;
			m_Graph = std::move(edited);
			return;
		}
		AnimationGraphData before = std::move(m_Graph);
		const size_t bytes = graph_bytes(before);
		m_History.push(std::move(before), bytes);
		m_Graph = std::move(edited);
	}

	template<typename F>
	bool GraphEditModel::apply(F&& change)
	{
		m_Error.clear();
		AnimationGraphData edited = m_Graph;
		const std::string why = change(edited);
		if (!why.empty())
		{
			return refuse(why);
		}
		std::string problem;
		// a graph that was already broken when it was opened is not policed, or none of it could be repaired (Save still asks for graph_valid)
		if (!model_valid(edited, problem) && model_valid(m_Graph, problem))
		{
			model_valid(edited, problem);
			return refuse(problem);
		}
		if (write_graph_file(edited) == write_graph_file(m_Graph))
		{
			return true; // nothing changed: no step
		}
		commit(std::move(edited));
		return true;
	}

	void GraphEditModel::begin_gesture()
	{
		if (!m_InGesture)
		{
			m_InGesture = true;
			m_GestureChanged = false;
			m_GestureBefore = m_Graph;
		}
	}

	void GraphEditModel::end_gesture()
	{
		if (m_InGesture)
		{
			m_InGesture = false;
			if (m_GestureChanged)
			{
				const size_t bytes = graph_bytes(m_GestureBefore);
				m_History.push(std::move(m_GestureBefore), bytes);
			}
			m_GestureBefore = AnimationGraphData{};
		}
	}

	void GraphEditModel::undo()
	{
		if (m_History.can_undo())
		{
			AnimationGraphData current = std::move(m_Graph);
			const size_t bytes = graph_bytes(current);
			m_Graph = m_History.undo(std::move(current), bytes);
		}
	}

	void GraphEditModel::redo()
	{
		if (m_History.can_redo())
		{
			AnimationGraphData current = std::move(m_Graph);
			const size_t bytes = graph_bytes(current);
			m_Graph = m_History.redo(std::move(current), bytes);
		}
	}

	bool GraphEditModel::set_skeleton(const AssetRef& skeleton)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				g.Skeleton = skeleton;
				return {};
			});
	}

	// ---- parameters

	bool GraphEditModel::add_parameter(const std::string& name, GraphParamType type, float defaultValue)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (!finite(defaultValue))
				{
					return "the default is not a number";
				}
				GraphParameter parameter;
				parameter.Name = trimmed(name);
				parameter.Type = type;
				parameter.Default = type == GraphParamType::Trigger ? 0.0f : type == GraphParamType::Bool ? (defaultValue != 0.0f ? 1.0f : 0.0f) : type == GraphParamType::Int ? std::trunc(defaultValue) : defaultValue;
				g.Parameters.push_back(std::move(parameter));
				return {};
			});
	}

	bool GraphEditModel::rename_parameter(size_t parameter, const std::string& name)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (parameter >= g.Parameters.size())
				{
					return NO_PARAMETER;
				}
				const std::string from = g.Parameters[parameter].Name;
				g.Parameters[parameter].Name = trimmed(name);
				if (g.Parameters[parameter].Name != from)
				{
					rewrite_parameter(g, from, g.Parameters[parameter].Name);
				}
				return {};
			});
	}

	bool GraphEditModel::set_parameter_default(size_t parameter, float value)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (parameter >= g.Parameters.size())
				{
					return NO_PARAMETER;
				}
				if (!finite(value))
				{
					return "the default is not a number";
				}
				GraphParameter& p = g.Parameters[parameter];
				p.Default = p.Type == GraphParamType::Trigger ? 0.0f : p.Type == GraphParamType::Bool ? (value != 0.0f ? 1.0f : 0.0f) : p.Type == GraphParamType::Int ? std::trunc(value) : value;
				return {};
			});
	}

	bool GraphEditModel::delete_parameter(size_t parameter)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (parameter >= g.Parameters.size())
				{
					return NO_PARAMETER;
				}
				const std::string name = g.Parameters[parameter].Name;
				const std::string used = where_used(g, name);
				if (!used.empty())
				{
					return "the parameter '" + name + "' is used by " + used;
				}
				g.Parameters.erase(g.Parameters.begin() + static_cast<std::ptrdiff_t>(parameter));
				return {};
			});
	}

	// ---- layers

	bool GraphEditModel::add_layer(const std::string& name, GraphLayerMode mode)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (mode == GraphLayerMode::Base)
				{
					return "only the first layer is the base layer";
				}
				GraphLayer layer;
				layer.Name = trimmed(name);
				layer.Mode = mode;
				GraphState state;
				state.Name = "State";
				layer.DefaultState = state.Name;
				layer.States.push_back(std::move(state));
				g.Layers.push_back(std::move(layer));
				return {};
			});
	}

	bool GraphEditModel::rename_layer(size_t layer, const std::string& name)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				l->Name = trimmed(name);
				return {};
			});
	}

	bool GraphEditModel::delete_layer(size_t layer)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (layer >= g.Layers.size())
				{
					return NO_LAYER;
				}
				if (layer == 0)
				{
					return "the base layer cannot be deleted";
				}
				g.Layers.erase(g.Layers.begin() + static_cast<std::ptrdiff_t>(layer));
				return {};
			});
	}

	bool GraphEditModel::set_layer_mode(size_t layer, GraphLayerMode mode)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				if (layer == 0 && mode != GraphLayerMode::Base)
				{
					return "the base layer's mode cannot change";
				}
				if (layer != 0 && mode == GraphLayerMode::Base)
				{
					return "only the first layer is the base layer";
				}
				l->Mode = mode;
				return {};
			});
	}

	bool GraphEditModel::set_layer_weight(size_t layer, float weight, const std::string& parameter)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				if (!finite(weight) || weight < 0.0f || weight > 1.0f)
				{
					return "the weight is between 0 and 1";
				}
				l->Weight = weight;
				l->WeightParameter = trimmed(parameter);
				return {};
			});
	}

	bool GraphEditModel::set_layer_mask(size_t layer, const std::string& root, float falloff, std::vector<GraphMaskJoint> joints)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				if (!finite(falloff) || falloff < 0.0f)
				{
					return "the falloff is not a number of 0 or more";
				}
				l->MaskRoot = root;
				l->MaskFalloff = falloff;
				l->MaskJoints = joints;
				return {};
			});
	}

	bool GraphEditModel::set_default_state(size_t layer, size_t state)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				g.Layers[layer].DefaultState = s->Name;
				return {};
			});
	}

	// ---- states

	bool GraphEditModel::add_state(size_t layer, const std::string& name)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				GraphState state;
				state.Name = trimmed(name);
				state.Position = glm::vec2(40.0f * static_cast<float>(l->States.size()), 40.0f);
				l->States.push_back(std::move(state));
				return {};
			});
	}

	bool GraphEditModel::rename_state(size_t layer, size_t state, const std::string& name)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				GraphLayer& l = g.Layers[layer];
				const std::string from = s->Name;
				s->Name = trimmed(name);
				if (s->Name != from)
				{
					for (GraphTransition& transition : l.Transitions)
					{
						if (transition.From == from) { transition.From = s->Name; }
						if (transition.To == from) { transition.To = s->Name; }
					}
					if (l.DefaultState == from)
					{
						l.DefaultState = s->Name;
					}
				}
				return {};
			});
	}

	bool GraphEditModel::delete_state(size_t layer, size_t state)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				GraphLayer& l = g.Layers[layer];
				if (l.States.size() == 1)
				{
					return "a layer needs at least one state";
				}
				const std::string name = s->Name;
				l.States.erase(l.States.begin() + static_cast<std::ptrdiff_t>(state));
				std::erase_if(l.Transitions, [&](const GraphTransition& t) { return t.From == name || t.To == name; });
				if (l.DefaultState == name)
				{
					l.DefaultState = l.States.front().Name;
				}
				return {};
			});
	}

	bool GraphEditModel::set_state_body(size_t layer, size_t state, GraphBodyKind kind)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (s->Body == kind)
				{
					return {};
				}
				if (kind == GraphBodyKind::Blend)
				{
					const auto axis = std::find_if(g.Parameters.begin(), g.Parameters.end(), [](const GraphParameter& p) { return p.Type == GraphParamType::Float; });
					if (axis == g.Parameters.end())
					{
						return "a blend space needs a float parameter for its X axis: add one first";
					}
					s->Body = GraphBodyKind::Blend;
					s->BlendX = axis->Name;
					s->BlendY.clear();
					s->Samples = { GraphBlendSample{ s->Clip, 0.0f, 0.0f } };
				}
				else
				{
					s->Body = GraphBodyKind::Clip;
					if (!s->Clip.valid() && !s->Samples.empty())
					{
						s->Clip = s->Samples.front().Clip;
					}
					s->Samples.clear();
					s->BlendX.clear();
					s->BlendY.clear();
				}
				return {};
			});
	}

	bool GraphEditModel::set_state_clip(size_t layer, size_t state, const AssetRef& clip)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (s->Body != GraphBodyKind::Clip)
				{
					return "a blend space has samples, not a clip";
				}
				s->Clip = clip;
				return {};
			});
	}

	bool GraphEditModel::set_state_options(size_t layer, size_t state, bool loop, float speed, const std::string& speedParameter)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (!finite(speed) || speed < 0.0f)
				{
					return "the speed is a number of 0 or more";
				}
				s->Loop = loop;
				s->Speed = speed;
				s->SpeedParameter = trimmed(speedParameter);
				return {};
			});
	}

	bool GraphEditModel::set_state_position(size_t layer, size_t state, const glm::vec2& position)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (!finite(position.x) || !finite(position.y))
				{
					return "the position is not a number";
				}
				s->Position = position;
				return {};
			});
	}

	bool GraphEditModel::add_sample(size_t layer, size_t state, const AssetRef& clip, float x, float y)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (s->Body != GraphBodyKind::Blend)
				{
					return "only a blend space has samples";
				}
				s->Samples.push_back(GraphBlendSample{ clip, x, y });
				return {};
			});
	}

	bool GraphEditModel::set_sample(size_t layer, size_t state, size_t sample, const AssetRef& clip, float x, float y)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (sample >= s->Samples.size())
				{
					return "there is no such sample";
				}
				s->Samples[sample] = GraphBlendSample{ clip, x, y };
				return {};
			});
	}

	bool GraphEditModel::delete_sample(size_t layer, size_t state, size_t sample)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (sample >= s->Samples.size())
				{
					return "there is no such sample";
				}
				s->Samples.erase(s->Samples.begin() + static_cast<std::ptrdiff_t>(sample));
				return {};
			});
	}

	bool GraphEditModel::set_blend_axes(size_t layer, size_t state, const std::string& x, const std::string& y)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphState* s = state_at(g, layer, state);
				if (s == nullptr)
				{
					return NO_STATE;
				}
				if (s->Body != GraphBodyKind::Blend)
				{
					return "only a blend space has axes";
				}
				s->BlendX = x;
				s->BlendY = y;
				return {};
			});
	}

	// ---- transitions

	bool GraphEditModel::add_transition(size_t layer, const std::string& from, const std::string& to)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphLayer* l = layer_at(g, layer);
				if (l == nullptr)
				{
					return NO_LAYER;
				}
				GraphTransition transition;
				transition.From = from;
				transition.To = to;
				l->Transitions.push_back(std::move(transition));
				return {};
			});
	}

	bool GraphEditModel::delete_transition(size_t layer, size_t transition)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				if (transition_at(g, layer, transition) == nullptr)
				{
					return NO_TRANSITION;
				}
				GraphLayer& l = g.Layers[layer];
				l.Transitions.erase(l.Transitions.begin() + static_cast<std::ptrdiff_t>(transition));
				return {};
			});
	}

	bool GraphEditModel::move_transition(size_t layer, size_t transition, size_t newIndex)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				GraphLayer& l = g.Layers[layer];
				if (newIndex >= l.Transitions.size())
				{
					return "there is no such place in the order";
				}
				GraphTransition moved = std::move(*t);
				l.Transitions.erase(l.Transitions.begin() + static_cast<std::ptrdiff_t>(transition));
				l.Transitions.insert(l.Transitions.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
				return {};
			});
	}

	bool GraphEditModel::set_transition_ends(size_t layer, size_t transition, const std::string& from, const std::string& to)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				t->From = from;
				t->To = to;
				return {};
			});
	}

	bool GraphEditModel::set_transition_options(size_t layer, size_t transition, float duration, float exitTime)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				if (!finite(duration) || duration < 0.0f)
				{
					return "the crossfade is a number of seconds, 0 or more";
				}
				if (!finite(exitTime) || exitTime > 1.0f)
				{
					return "the exit time is a fraction between 0 and 1 (below 0: none)";
				}
				t->Duration = duration;
				t->ExitTime = exitTime < 0.0f ? -1.0f : exitTime;
				return {};
			});
	}

	bool GraphEditModel::add_condition(size_t layer, size_t transition, const GraphCondition& condition)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				t->Conditions.push_back(condition);
				return {};
			});
	}

	bool GraphEditModel::set_condition(size_t layer, size_t transition, size_t condition, const GraphCondition& value)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				if (condition >= t->Conditions.size())
				{
					return "there is no such condition";
				}
				t->Conditions[condition] = value;
				return {};
			});
	}

	bool GraphEditModel::delete_condition(size_t layer, size_t transition, size_t condition)
	{
		return apply([&](AnimationGraphData& g) -> std::string
			{
				GraphTransition* t = transition_at(g, layer, transition);
				if (t == nullptr)
				{
					return NO_TRANSITION;
				}
				if (condition >= t->Conditions.size())
				{
					return "there is no such condition";
				}
				t->Conditions.erase(t->Conditions.begin() + static_cast<std::ptrdiff_t>(condition));
				return {};
			});
	}
}
