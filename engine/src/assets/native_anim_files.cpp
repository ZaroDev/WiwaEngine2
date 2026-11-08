#include <pch.h>
#include "native_anim_files.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "wasset_io.h"

namespace wiwa
{
	namespace
	{
		using json = nlohmann::json;

		// The shortest decimal text that reads back as the same float, so that files stay readable (0.1, not 0.10000000149011612).
		double short_float(float value)
		{
			char text[32];
			for (int precision = 1; precision <= 9; precision++)
			{
				std::snprintf(text, sizeof(text), "%.*g", precision, static_cast<double>(value));
				if (std::strtof(text, nullptr) == value)
				{
					break;
				}
			}
			return std::strtod(text, nullptr);
		}

		json float_array(const float* values, size_t count)
		{
			json result = json::array();
			for (size_t i = 0; i < count; i++)
			{
				result.push_back(short_float(values[i]));
			}
			return result;
		}

		// Reads exactly count finite numbers.
		bool read_floats(const json& value, float* out, size_t count)
		{
			if (!value.is_array() || value.size() != count)
			{
				return false;
			}
			for (size_t i = 0; i < count; i++)
			{
				if (!value[i].is_number())
				{
					return false;
				}
				out[i] = value[i].get<float>();
				if (!std::isfinite(out[i]))
				{
					return false;
				}
			}
			return true;
		}

		template<typename T>
		std::optional<T> read_with_header(std::span<const std::byte> bytes, u32 magicWanted, u32 versionWanted, const char* what, std::string& error)
		{
			BinaryArchive ar = BinaryArchive::reader(bytes);
			u32 magic = 0;
			u32 version = 0;
			ar.value(magic);
			ar.value(version);
			if (!ar.ok() || magic != magicWanted)
			{
				error = std::string("this is not a ") + what + " file";
				return std::nullopt;
			}
			if (version != versionWanted)
			{
				error = std::string(what) + " file version " + std::to_string(version) + " is not supported (this editor reads version " + std::to_string(versionWanted) + ")";
				return std::nullopt;
			}
			T data{};
			archive(ar, data);
			if (!ar.ok() || ar.remaining() != 0)
			{
				error = std::string("the ") + what + " file is damaged or truncated";
				return std::nullopt;
			}
			return data;
		}

		template<typename T>
		std::vector<std::byte> write_with_header(const T& data, u32 magic, u32 version)
		{
			BinaryArchive ar = BinaryArchive::writer();
			u32 magicValue = magic;
			u32 versionValue = version;
			ar.value(magicValue);
			ar.value(versionValue);
			archive(ar, const_cast<T&>(data)); // writing never modifies the data
			return ar.take();
		}
	}

	std::string write_skeleton_file(const SkeletonData& skeleton)
	{
		json root = json::object();
		root["version"] = WSKEL_VERSION;
		root["name"] = skeleton.Name;
		json joints = json::array();
		for (size_t j = 0; j < skeleton.JointNames.size(); j++)
		{
			json joint = json::object();
			joint["name"] = skeleton.JointNames[j];
			joint["parent"] = j < skeleton.Parents.size() ? skeleton.Parents[j] : -1;
			if (j < skeleton.RestTranslation.size())
			{
				joint["t"] = float_array(&skeleton.RestTranslation[j].x, 3);
			}
			if (j < skeleton.RestRotation.size())
			{
				const glm::quat& q = skeleton.RestRotation[j];
				const float xyzw[4] = { q.x, q.y, q.z, q.w };
				joint["r"] = float_array(xyzw, 4);
			}
			if (j < skeleton.RestScale.size())
			{
				joint["s"] = float_array(&skeleton.RestScale[j].x, 3);
			}
			if (j < skeleton.Pre.size())
			{
				joint["pre"] = float_array(&skeleton.Pre[j][0][0], 16);
			}
			if (j < skeleton.InverseBind.size())
			{
				joint["inverseBind"] = float_array(&skeleton.InverseBind[j][0][0], 16);
			}
			joints.push_back(std::move(joint));
		}
		root["joints"] = std::move(joints);
		if (!skeleton.Sockets.empty())
		{
			json sockets = json::array();
			for (const SkeletonSocket& socket : skeleton.Sockets)
			{
				json item = json::object();
				item["name"] = socket.Name;
				item["joint"] = socket.Joint;
				item["t"] = float_array(&socket.Position.x, 3);
				const float xyzw[4] = { socket.Rotation.x, socket.Rotation.y, socket.Rotation.z, socket.Rotation.w };
				item["r"] = float_array(xyzw, 4);
				item["s"] = float_array(&socket.Scale.x, 3);
				sockets.push_back(std::move(item));
			}
			root["sockets"] = std::move(sockets);
		}
		return root.dump(1, '\t') + "\n";
	}

	std::optional<SkeletonData> read_skeleton_file(std::string_view text, std::string& error)
	{
		json root = json::parse(text.begin(), text.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "the skeleton file is not valid JSON";
			return std::nullopt;
		}
		if (!root.contains("version") || !root["version"].is_number_integer() || (root["version"].get<int>() < 1 || root["version"].get<int>() > WSKEL_VERSION))
		{
			error = "the skeleton file version is missing or not supported (this editor reads version " + std::to_string(WSKEL_VERSION) + ")";
			return std::nullopt;
		}
		if (!root.contains("joints") || !root["joints"].is_array())
		{
			error = "the skeleton file has no joints list";
			return std::nullopt;
		}

		SkeletonData skeleton;
		skeleton.Name = root.value("name", std::string{});
		std::unordered_set<std::string> names;
		const json& joints = root["joints"];
		for (size_t j = 0; j < joints.size(); j++)
		{
			const json& joint = joints[j];
			const std::string label = "joint " + std::to_string(j);
			if (!joint.is_object() || !joint.contains("name") || !joint["name"].is_string() || joint["name"].get<std::string>().empty())
			{
				error = label + " has no name";
				return std::nullopt;
			}
			const std::string name = joint["name"].get<std::string>();
			if (!names.insert(name).second)
			{
				error = "the joint name '" + name + "' is used twice";
				return std::nullopt;
			}
			if (!joint.contains("parent") || !joint["parent"].is_number_integer())
			{
				error = "the joint '" + name + "' has no parent index (-1 for a root)";
				return std::nullopt;
			}
			const i32 parent = joint["parent"].get<i32>();
			if (parent < -1 || parent >= static_cast<i32>(j))
			{
				error = "the joint '" + name + "' has a parent that does not come before it";
				return std::nullopt;
			}

			glm::vec3 t(0.0f);
			glm::vec3 s(1.0f);
			float xyzw[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			glm::mat4 pre(1.0f);
			glm::mat4 inverseBind(1.0f);
			const bool good = (!joint.contains("t") || read_floats(joint["t"], &t.x, 3))
				&& (!joint.contains("r") || read_floats(joint["r"], xyzw, 4))
				&& (!joint.contains("s") || read_floats(joint["s"], &s.x, 3))
				&& (!joint.contains("pre") || read_floats(joint["pre"], &pre[0][0], 16))
				&& (!joint.contains("inverseBind") || read_floats(joint["inverseBind"], &inverseBind[0][0], 16));
			if (!good)
			{
				error = "the joint '" + name + "' has a number that is missing, not finite or the wrong size";
				return std::nullopt;
			}

			skeleton.JointNames.push_back(name);
			skeleton.Parents.push_back(parent);
			skeleton.RestTranslation.push_back(t);
			skeleton.RestRotation.push_back(glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]));
			skeleton.RestScale.push_back(s);
			skeleton.Pre.push_back(pre);
			skeleton.InverseBind.push_back(inverseBind);
		}

		if (root.contains("sockets"))
		{
			if (!root["sockets"].is_array())
			{
				error = "the sockets of the skeleton file are not a list";
				return std::nullopt;
			}
			std::unordered_set<std::string> socketNames;
			for (const json& item : root["sockets"])
			{
				if (!item.is_object() || !item.contains("name") || !item["name"].is_string() || item["name"].get<std::string>().empty())
				{
					error = "a socket has no name";
					return std::nullopt;
				}
				SkeletonSocket socket;
				socket.Name = item["name"].get<std::string>();
				if (!socketNames.insert(socket.Name).second)
				{
					error = "the socket name '" + socket.Name + "' is used twice";
					return std::nullopt;
				}
				if (!item.contains("joint") || !item["joint"].is_string() || !names.contains(item["joint"].get<std::string>()))
				{
					error = "the socket '" + socket.Name + "' is not on a joint of the skeleton";
					return std::nullopt;
				}
				socket.Joint = item["joint"].get<std::string>();
				float xyzw[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				const bool good = (!item.contains("t") || read_floats(item["t"], &socket.Position.x, 3))
					&& (!item.contains("r") || read_floats(item["r"], xyzw, 4))
					&& (!item.contains("s") || read_floats(item["s"], &socket.Scale.x, 3));
				const float length = std::sqrt(xyzw[0] * xyzw[0] + xyzw[1] * xyzw[1] + xyzw[2] * xyzw[2] + xyzw[3] * xyzw[3]);
				if (!good || !(length > 0.0f))
				{
					error = "the socket '" + socket.Name + "' has a number that is missing, not finite or the wrong size, or a zero rotation";
					return std::nullopt;
				}
				socket.Rotation = glm::normalize(glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]));
				skeleton.Sockets.push_back(std::move(socket));
			}
		}
		return skeleton;
	}

	std::vector<std::byte> write_clip_file(const AnimationClipData& clip)
	{
		return write_with_header(clip, WANIM_MAGIC, WANIM_VERSION);
	}

	std::optional<AnimationClipData> read_clip_file(std::span<const std::byte> bytes, std::string& error)
	{
		std::optional<AnimationClipData> clip = read_with_header<AnimationClipData>(bytes, WANIM_MAGIC, WANIM_VERSION, "animation clip (.wanim)", error);
		if (!clip)
		{
			return std::nullopt;
		}

		// the evaluator indexes these arrays without looking: a channel has to say how many numbers a key has and carry them all
		if (!std::isfinite(clip->Duration) || clip->Duration < 0.0f)
		{
			error = "the clip has a duration that is not a finite, non-negative number";
			return std::nullopt;
		}
		for (const AnimationChannel& channel : clip->Channels)
		{
			const bool trs = channel.Path != AnimationPath::Weights;
			const u32 wanted = channel.Path == AnimationPath::Rotation ? 4u : 3u;
			const size_t perKey = channel.Interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
			if (channel.Joint.empty() || channel.Times.empty() || (trs && channel.Components != wanted) || channel.Components == 0
				|| channel.Values.size() < channel.Times.size() * channel.Components * perKey)
			{
				error = "the channel of the joint '" + channel.Joint + "' is incomplete (keys, components or values do not fit)";
				return std::nullopt;
			}
			for (size_t i = 0; i < channel.Times.size(); i++)
			{
				if (!std::isfinite(channel.Times[i]) || (i > 0 && channel.Times[i] < channel.Times[i - 1]))
				{
					error = "the channel of the joint '" + channel.Joint + "' has key times that are not in order";
					return std::nullopt;
				}
			}
		}
		for (size_t i = 0; i < clip->Events.size(); i++)
		{
			if (!std::isfinite(clip->Events[i].Time) || (i > 0 && clip->Events[i].Time < clip->Events[i - 1].Time))
			{
				error = "the clip's events are not in time order";
				return std::nullopt;
			}
		}
		return clip;
	}

	std::vector<std::byte> write_mesh_file(const MeshData& mesh)
	{
		return write_with_header(mesh, WMESH_MAGIC, WMESH_VERSION);
	}

	std::optional<MeshData> read_mesh_file(std::span<const std::byte> bytes, std::string& error)
	{
		std::optional<MeshData> mesh = read_with_header<MeshData>(bytes, WMESH_MAGIC, WMESH_VERSION, "skinned mesh (.wmesh)", error);
		if (!mesh)
		{
			return std::nullopt;
		}
		if (!mesh->SkinVertices.empty() && mesh->SkinVertices.size() != mesh->Vertices.size())
		{
			error = "the mesh has skin data for a different number of vertices than it has";
			return std::nullopt;
		}
		if (!mesh->SkinVertices.empty() && mesh->SkinJointNames.empty())
		{
			error = "the mesh has skin data but no joint list to check it against";
			return std::nullopt;
		}
		if (!mesh->SkinJointNames.empty())
		{
			for (const MeshSkinVertex& vertex : mesh->SkinVertices)
			{
				for (int k = 0; k < 4; k++)
				{
					if (vertex.Joints[k] >= mesh->SkinJointNames.size())
					{
						error = "a vertex names a joint that is not in the mesh's joint list";
						return std::nullopt;
					}
				}
			}
		}
		return mesh;
	}

	// ---- .wgraph
	namespace
	{
		const char* param_type_name(GraphParamType type)
		{
			switch (type)
			{
			case GraphParamType::Float: return "float";
			case GraphParamType::Int: return "int";
			case GraphParamType::Bool: return "bool";
			case GraphParamType::Trigger: return "trigger";
			}
			return "float";
		}

		const char* op_name(GraphCompareOp op)
		{
			switch (op)
			{
			case GraphCompareOp::Greater: return ">";
			case GraphCompareOp::Less: return "<";
			case GraphCompareOp::GreaterEqual: return ">=";
			case GraphCompareOp::LessEqual: return "<=";
			case GraphCompareOp::Equal: return "==";
			case GraphCompareOp::NotEqual: return "!=";
			case GraphCompareOp::IsTrue: return "true";
			case GraphCompareOp::IsFalse: return "false";
			case GraphCompareOp::Fired: return "fired";
			}
			return ">";
		}

		const char* mode_name(GraphLayerMode mode)
		{
			return mode == GraphLayerMode::Base ? "base" : mode == GraphLayerMode::Override ? "override" : "additive";
		}

		json ref_json(const AssetRef& ref)
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			json result = json::object();
			result["guid"] = ref.Guid.to_string();
			if (ref.LocalId != 0)
			{
				result["local"] = ref.LocalId;
			}
			return result;
		}

		// null/absent is an invalid reference; anything else must be {guid, local?}.
		bool read_ref(const json& value, AssetRef& out)
		{
			out = AssetRef{};
			if (value.is_null())
			{
				return true;
			}
			if (!value.is_object() || !value.contains("guid") || !value["guid"].is_string())
			{
				return false;
			}
			const std::optional<AssetGuid> guid = AssetGuid::from_string(value["guid"].get<std::string>());
			if (!guid)
			{
				return false;
			}
			out.Guid = *guid;
			if (value.contains("local"))
			{
				if (!value["local"].is_number_unsigned())
				{
					return false;
				}
				out.LocalId = value["local"].get<u64>();
			}
			return true;
		}

		bool finite_number(const json& object, const char* key, float& out)
		{
			if (!object.contains(key) || !object[key].is_number())
			{
				return false;
			}
			out = object[key].get<float>();
			return std::isfinite(out);
		}

		bool valid_name(const std::string& name)
		{
			return !name.empty() && name.size() <= 64;
		}

		template<typename T>
		bool names_unique(const std::vector<T>& list, std::string& duplicate)
		{
			std::unordered_set<std::string> seen;
			for (const T& item : list)
			{
				if (!seen.insert(item.Name).second)
				{
					duplicate = item.Name;
					return false;
				}
			}
			return true;
		}
	}

	std::string write_graph_file(const AnimationGraphData& graph)
	{
		json root = json::object();
		root["version"] = WGRAPH_VERSION;
		root["name"] = graph.Name;
		root["skeleton"] = ref_json(graph.Skeleton);
		json parameters = json::array();
		for (const GraphParameter& parameter : graph.Parameters)
		{
			json item = json::object();
			item["name"] = parameter.Name;
			item["type"] = param_type_name(parameter.Type);
			item["default"] = short_float(parameter.Default);
			parameters.push_back(item);
		}
		root["parameters"] = parameters;

		json layers = json::array();
		for (const GraphLayer& layer : graph.Layers)
		{
			json item = json::object();
			item["name"] = layer.Name;
			item["mode"] = mode_name(layer.Mode);
			item["weight"] = short_float(layer.Weight);
			if (!layer.WeightParameter.empty())
			{
				item["weightParameter"] = layer.WeightParameter;
			}
			json mask = json::object();
			mask["root"] = layer.MaskRoot;
			mask["falloff"] = short_float(layer.MaskFalloff);
			json joints = json::array();
			for (const GraphMaskJoint& joint : layer.MaskJoints)
			{
				json entry = json::object();
				entry["joint"] = joint.Joint;
				entry["weight"] = short_float(joint.Weight);
				joints.push_back(entry);
			}
			mask["joints"] = joints;
			item["mask"] = mask;

			json machine = json::object();
			machine["default"] = layer.DefaultState;
			json states = json::array();
			for (const GraphState& state : layer.States)
			{
				json entry = json::object();
				entry["name"] = state.Name;
				json body = json::object();
				if (state.Body == GraphBodyKind::Clip)
				{
					body["clip"] = ref_json(state.Clip);
				}
				else
				{
					json blend = json::object();
					blend["x"] = state.BlendX;
					blend["y"] = state.BlendY;
					json samples = json::array();
					for (const GraphBlendSample& sample : state.Samples)
					{
						json s = json::object();
						s["clip"] = ref_json(sample.Clip);
						s["x"] = short_float(sample.X);
						s["y"] = short_float(sample.Y);
						samples.push_back(s);
					}
					blend["samples"] = samples;
					body["blend"] = blend;
				}
				entry["body"] = body;
				entry["loop"] = state.Loop;
				entry["speed"] = short_float(state.Speed);
				if (!state.SpeedParameter.empty())
				{
					entry["speedParameter"] = state.SpeedParameter;
				}
				entry["position"] = float_array(&state.Position.x, 2);
				states.push_back(entry);
			}
			machine["states"] = states;
			json transitions = json::array();
			for (const GraphTransition& transition : layer.Transitions)
			{
				json entry = json::object();
				if (!transition.From.empty())
				{
					entry["from"] = transition.From;
				}
				entry["to"] = transition.To;
				entry["duration"] = short_float(transition.Duration);
				if (transition.ExitTime >= 0.0f)
				{
					entry["exitTime"] = short_float(transition.ExitTime);
				}
				json conditions = json::array();
				for (const GraphCondition& condition : transition.Conditions)
				{
					json c = json::object();
					c["parameter"] = condition.Parameter;
					c["op"] = op_name(condition.Op);
					c["value"] = short_float(condition.Value);
					conditions.push_back(c);
				}
				entry["conditions"] = conditions;
				transitions.push_back(entry);
			}
			machine["transitions"] = transitions;
			item["machine"] = machine;
			layers.push_back(item);
		}
		root["layers"] = layers;
		return root.dump(1, '\t');
	}

	std::optional<AnimationGraphData> read_graph_file(std::string_view text, std::string& error, bool validate)
	{
		json root = json::parse(text.begin(), text.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "the graph file is not valid JSON";
			return std::nullopt;
		}
		if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() < 1 || root["version"].get<int>() > WGRAPH_VERSION)
		{
			error = "the graph file version is missing or not supported (this editor reads version " + std::to_string(WGRAPH_VERSION) + ")";
			return std::nullopt;
		}

		AnimationGraphData graph;
		graph.Name = root.value("name", std::string{});
		if (root.contains("skeleton") && !read_ref(root["skeleton"], graph.Skeleton))
		{
			error = "the skeleton reference is not a guid";
			return std::nullopt;
		}

		if (root.contains("parameters"))
		{
			if (!root["parameters"].is_array())
			{
				error = "parameters is not a list";
				return std::nullopt;
			}
			for (const json& item : root["parameters"])
			{
				GraphParameter parameter;
				if (!item.is_object() || !item.contains("name") || !item["name"].is_string() || !item.contains("type") || !item["type"].is_string())
				{
					error = "a parameter has no name or type";
					return std::nullopt;
				}
				parameter.Name = item["name"].get<std::string>();
				const std::string type = item["type"].get<std::string>();
				if (type == "float") { parameter.Type = GraphParamType::Float; }
				else if (type == "int") { parameter.Type = GraphParamType::Int; }
				else if (type == "bool") { parameter.Type = GraphParamType::Bool; }
				else if (type == "trigger") { parameter.Type = GraphParamType::Trigger; }
				else
				{
					error = "the parameter '" + parameter.Name + "' has an unknown type '" + type + "'";
					return std::nullopt;
				}
				if (item.contains("default") && !finite_number(item, "default", parameter.Default))
				{
					error = "the default of parameter '" + parameter.Name + "' is not a number";
					return std::nullopt;
				}
				graph.Parameters.push_back(std::move(parameter));
			}
		}

		if (!root.contains("layers") || !root["layers"].is_array())
		{
			error = "the graph file has no layers list";
			return std::nullopt;
		}
		for (const json& item : root["layers"])
		{
			GraphLayer layer;
			if (!item.is_object() || !item.contains("name") || !item["name"].is_string())
			{
				error = "a layer has no name";
				return std::nullopt;
			}
			layer.Name = item["name"].get<std::string>();
			const std::string label = "layer '" + layer.Name + "'";
			const std::string mode = item.value("mode", std::string("base"));
			if (mode == "base") { layer.Mode = GraphLayerMode::Base; }
			else if (mode == "override") { layer.Mode = GraphLayerMode::Override; }
			else if (mode == "additive") { layer.Mode = GraphLayerMode::Additive; }
			else
			{
				error = label + " has an unknown mode '" + mode + "'";
				return std::nullopt;
			}
			if (item.contains("weight") && !finite_number(item, "weight", layer.Weight))
			{
				error = label + " has a weight that is not a number";
				return std::nullopt;
			}
			layer.WeightParameter = item.value("weightParameter", std::string{});
			if (item.contains("mask"))
			{
				const json& mask = item["mask"];
				if (!mask.is_object())
				{
					error = label + " has a mask that is not an object";
					return std::nullopt;
				}
				layer.MaskRoot = mask.value("root", std::string{});
				if (mask.contains("falloff") && !finite_number(mask, "falloff", layer.MaskFalloff))
				{
					error = label + " has a mask falloff that is not a number";
					return std::nullopt;
				}
				if (mask.contains("joints"))
				{
					if (!mask["joints"].is_array())
					{
						error = label + " has mask joints that are not a list";
						return std::nullopt;
					}
					for (const json& entry : mask["joints"])
					{
						GraphMaskJoint joint;
						if (!entry.is_object() || !entry.contains("joint") || !entry["joint"].is_string() || !finite_number(entry, "weight", joint.Weight))
						{
							error = label + " has a mask joint without a name or a weight";
							return std::nullopt;
						}
						joint.Joint = entry["joint"].get<std::string>();
						layer.MaskJoints.push_back(std::move(joint));
					}
				}
			}

			if (!item.contains("machine") || !item["machine"].is_object())
			{
				error = label + " has no machine";
				return std::nullopt;
			}
			const json& machine = item["machine"];
			layer.DefaultState = machine.value("default", std::string{});
			if (machine.contains("states"))
			{
				if (!machine["states"].is_array())
				{
					error = label + " has states that are not a list";
					return std::nullopt;
				}
				for (const json& entry : machine["states"])
				{
					GraphState state;
					if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string())
					{
						error = label + " has a state without a name";
						return std::nullopt;
					}
					state.Name = entry["name"].get<std::string>();
					const std::string where = label + ", state '" + state.Name + "'";
					if (!entry.contains("body") || !entry["body"].is_object())
					{
						error = where + " has no body";
						return std::nullopt;
					}
					const json& body = entry["body"];
					if (body.contains("clip"))
					{
						state.Body = GraphBodyKind::Clip;
						if (!read_ref(body["clip"], state.Clip))
						{
							error = where + " has a clip that is not a guid";
							return std::nullopt;
						}
					}
					else if (body.contains("blend") && body["blend"].is_object())
					{
						state.Body = GraphBodyKind::Blend;
						const json& blend = body["blend"];
						state.BlendX = blend.value("x", std::string{});
						state.BlendY = blend.value("y", std::string{});
						if (blend.contains("samples") && blend["samples"].is_array())
						{
							for (const json& s : blend["samples"])
							{
								GraphBlendSample sample;
								if (!s.is_object() || !s.contains("clip") || !read_ref(s["clip"], sample.Clip) || !finite_number(s, "x", sample.X)
									|| (s.contains("y") && !finite_number(s, "y", sample.Y)))
								{
									error = where + " has a blend sample without a clip or a position";
									return std::nullopt;
								}
								state.Samples.push_back(sample);
							}
						}
						else
						{
							error = where + " has a blend without samples";
							return std::nullopt;
						}
					}
					else
					{
						error = where + " has a body that is neither a clip nor a blend";
						return std::nullopt;
					}
					state.Loop = entry.value("loop", true);
					if (entry.contains("speed") && !finite_number(entry, "speed", state.Speed))
					{
						error = where + " has a speed that is not a number";
						return std::nullopt;
					}
					state.SpeedParameter = entry.value("speedParameter", std::string{});
					if (entry.contains("position") && !read_floats(entry["position"], &state.Position.x, 2))
					{
						error = where + " has a position that is not two numbers";
						return std::nullopt;
					}
					layer.States.push_back(std::move(state));
				}
			}
			if (machine.contains("transitions"))
			{
				if (!machine["transitions"].is_array())
				{
					error = label + " has transitions that are not a list";
					return std::nullopt;
				}
				for (const json& entry : machine["transitions"])
				{
					GraphTransition transition;
					if (!entry.is_object() || !entry.contains("to") || !entry["to"].is_string())
					{
						error = label + " has a transition without a target";
						return std::nullopt;
					}
					transition.From = entry.value("from", std::string{});
					transition.To = entry["to"].get<std::string>();
					if (entry.contains("duration") && !finite_number(entry, "duration", transition.Duration))
					{
						error = label + " has a transition duration that is not a number";
						return std::nullopt;
					}
					if (entry.contains("exitTime") && !finite_number(entry, "exitTime", transition.ExitTime))
					{
						error = label + " has an exit time that is not a number";
						return std::nullopt;
					}
					if (entry.contains("conditions"))
					{
						if (!entry["conditions"].is_array())
						{
							error = label + " has conditions that are not a list";
							return std::nullopt;
						}
						for (const json& c : entry["conditions"])
						{
							GraphCondition condition;
							if (!c.is_object() || !c.contains("parameter") || !c["parameter"].is_string() || !c.contains("op") || !c["op"].is_string())
							{
								error = label + " has a condition without a parameter or an operator";
								return std::nullopt;
							}
							condition.Parameter = c["parameter"].get<std::string>();
							const std::string op = c["op"].get<std::string>();
							bool known = false;
							for (u32 i = 0; i <= static_cast<u32>(GraphCompareOp::Fired); i++)
							{
								if (op == op_name(static_cast<GraphCompareOp>(i)))
								{
									condition.Op = static_cast<GraphCompareOp>(i);
									known = true;
								}
							}
							if (!known)
							{
								error = label + " has an unknown operator '" + op + "'";
								return std::nullopt;
							}
							if (c.contains("value") && !finite_number(c, "value", condition.Value))
							{
								error = label + " has a condition value that is not a number";
								return std::nullopt;
							}
							transition.Conditions.push_back(std::move(condition));
						}
					}
					layer.Transitions.push_back(std::move(transition));
				}
			}
			graph.Layers.push_back(std::move(layer));
		}

		if (validate && !graph_valid(graph, error))
		{
			return std::nullopt;
		}
		return graph;
	}

	bool graph_valid(const AnimationGraphData& graph, std::string& error)
	{
		error.clear();
		std::string duplicate;
		for (const GraphParameter& parameter : graph.Parameters)
		{
			if (!valid_name(parameter.Name))
			{
				error = "a parameter name is empty or longer than 64 characters";
				return false;
			}
		}
		if (!names_unique(graph.Parameters, duplicate))
		{
			error = "the parameter name '" + duplicate + "' is used twice";
			return false;
		}
		const auto parameter_type = [&](const std::string& name, GraphParamType& type) -> bool
		{
			for (const GraphParameter& parameter : graph.Parameters)
			{
				if (parameter.Name == name)
				{
					type = parameter.Type;
					return true;
				}
			}
			return false;
		};
		const auto float_parameter = [&](const std::string& name, const std::string& where, std::string& problem) -> bool
		{
			if (name.empty())
			{
				return true;
			}
			GraphParamType type;
			if (!parameter_type(name, type))
			{
				problem = where + " names the parameter '" + name + "', which does not exist";
				return false;
			}
			if (type != GraphParamType::Float)
			{
				problem = where + " needs a float parameter, '" + name + "' is not one";
				return false;
			}
			return true;
		};

		if (graph.Layers.empty())
		{
			error = "the graph has no layers";
			return false;
		}
		for (const GraphParameter& parameter : graph.Parameters)
		{
			if (!std::isfinite(parameter.Default))
			{
				error = "the default of parameter '" + parameter.Name + "' is not a number";
				return false;
			}
		}
		for (const GraphLayer& layer : graph.Layers)
		{
			if (!valid_name(layer.Name))
			{
				error = "a layer name is empty or longer than 64 characters";
				return false;
			}
		}
		if (!names_unique(graph.Layers, duplicate))
		{
			error = "the layer name '" + duplicate + "' is used twice";
			return false;
		}
		for (size_t l = 0; l < graph.Layers.size(); l++)
		{
			const GraphLayer& layer = graph.Layers[l];
			const std::string label = "layer '" + layer.Name + "'";
			if ((l == 0) != (layer.Mode == GraphLayerMode::Base))
			{
				error = l == 0 ? "the first layer must be the base layer" : label + " cannot be a base layer";
				return false;
			}
			if (l == 0 && (!layer.MaskRoot.empty() || !layer.MaskJoints.empty()))
			{
				error = "the base layer has no mask";
				return false;
			}
			if (!std::isfinite(layer.Weight) || !std::isfinite(layer.MaskFalloff) || layer.MaskFalloff < 0.0f)
			{
				error = label + " has a weight or a mask falloff that is not valid";
				return false;
			}
			for (const GraphMaskJoint& joint : layer.MaskJoints)
			{
				if (joint.Joint.empty() || !(joint.Weight >= 0.0f && joint.Weight <= 1.0f))
				{
					error = label + " has a mask joint without a name or a weight outside 0..1";
					return false;
				}
			}
			std::string problem;
			if (!float_parameter(layer.WeightParameter, label + "'s weight", problem))
			{
				error = problem;
				return false;
			}
			if (layer.States.empty())
			{
				error = label + " has no states";
				return false;
			}
			for (const GraphState& state : layer.States)
			{
				if (!valid_name(state.Name))
				{
					error = label + " has a state name that is empty or longer than 64 characters";
					return false;
				}
			}
			if (!names_unique(layer.States, duplicate))
			{
				error = label + " uses the state name '" + duplicate + "' twice";
				return false;
			}
			const auto has_state = [&](const std::string& name)
			{
				return std::any_of(layer.States.begin(), layer.States.end(), [&](const GraphState& state) { return state.Name == name; });
			};
			if (!has_state(layer.DefaultState))
			{
				error = label + " has no default state named '" + layer.DefaultState + "'";
				return false;
			}
			for (const GraphState& state : layer.States)
			{
				const std::string where = label + ", state '" + state.Name + "'";
				if (!std::isfinite(state.Speed) || state.Speed < 0.0f)
				{
					error = where + " has a speed that is not valid";
					return false;
				}
				if (!float_parameter(state.SpeedParameter, where + "'s speed", problem))
				{
					error = problem;
					return false;
				}
				if (state.Body == GraphBodyKind::Clip)
				{
					if (!state.Clip.valid())
					{
						error = where + " has no clip";
						return false;
					}
				}
				else
				{
					if (state.Samples.empty())
					{
						error = where + " is a blend space without samples";
						return false;
					}
					if (state.BlendX.empty() || !float_parameter(state.BlendX, where + "'s blend X", problem) || !float_parameter(state.BlendY, where + "'s blend Y", problem))
					{
						error = problem.empty() ? where + " has no X parameter for its blend space" : problem;
						return false;
					}
					for (const GraphBlendSample& sample : state.Samples)
					{
						if (!sample.Clip.valid() || !std::isfinite(sample.X) || !std::isfinite(sample.Y))
						{
							error = where + " has a blend sample without a clip or with a position that is not a number";
							return false;
						}
					}
				}
			}
			for (const GraphTransition& transition : layer.Transitions)
			{
				const std::string where = label + ", transition to '" + transition.To + "'";
				if ((!transition.From.empty() && !has_state(transition.From)) || !has_state(transition.To))
				{
					error = where + " names a state that does not exist";
					return false;
				}
				if (!std::isfinite(transition.Duration) || transition.Duration < 0.0f || !std::isfinite(transition.ExitTime) || transition.ExitTime > 1.0f)
				{
					error = where + " has a duration or an exit time that is not valid";
					return false;
				}
				for (const GraphCondition& condition : transition.Conditions)
				{
					GraphParamType type;
					if (!parameter_type(condition.Parameter, type))
					{
						error = where + " has a condition on the parameter '" + condition.Parameter + "', which does not exist";
						return false;
					}
					const bool boolOp = condition.Op == GraphCompareOp::IsTrue || condition.Op == GraphCompareOp::IsFalse;
					const bool fits = condition.Op == GraphCompareOp::Fired ? type == GraphParamType::Trigger
						: boolOp ? type == GraphParamType::Bool
						: (type == GraphParamType::Float || type == GraphParamType::Int);
					if (!fits || !std::isfinite(condition.Value))
					{
						error = where + " has a condition on '" + condition.Parameter + "' whose operator does not fit its type";
						return false;
					}
				}
			}
		}
		return true;
	}
}
