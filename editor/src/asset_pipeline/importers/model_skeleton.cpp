#include <editor_common.h>
#include "model_skeleton.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace wiwa
{
	namespace
	{
		glm::mat4 local_matrix(const PrefabNode& node)
		{
			return glm::translate(glm::mat4(1.0f), node.Translation) * glm::mat4_cast(node.Rotation) * glm::scale(glm::mat4(1.0f), node.Scale);
		}

		void decompose(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
		{
			translation = glm::vec3(matrix[3]);
			glm::vec3 axes[3] = { glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2]) };
			scale = glm::vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
			for (int i = 0; i < 3; i++)
			{
				axes[i] = scale[i] > 1e-8f ? axes[i] / scale[i] : axes[i];
			}
			rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
		}

		void finalize_skeleton(ImportedModel& model, size_t index, std::vector<int>& meshRemappedBy)
		{
			ImportedSkeleton& skeleton = model.Skeletons[index];
			const size_t count = skeleton.Joints.size();
			if (skeleton.InverseBind.size() != count)
			{
				skeleton.InverseBind.resize(count, glm::mat4(1.0f));
			}
			if (count > SKELETON_MAX_JOINTS)
			{
				model.Warnings.push_back("The skeleton " + skeleton.Name + " has " + std::to_string(count) + " joints; only " + std::to_string(SKELETON_MAX_JOINTS) + " are supported");
			}

			std::unordered_map<u32, size_t> jointOfNode;
			for (size_t j = 0; j < count; j++)
			{
				jointOfNode.emplace(skeleton.Joints[j], j);
			}

			// the parent joint and the rest pose of every joint, in the order the readers listed them
			std::vector<i32> parents(count, -1);
			std::vector<glm::mat4> pre(count, glm::mat4(1.0f));
			for (size_t j = 0; j < count; j++)
			{
				const u32 node = skeleton.Joints[j];
				if (node >= model.Nodes.size())
				{
					continue;
				}
				glm::mat4 matrix(1.0f); // the nodes between this joint and its parent joint, or the root
				i32 up = model.Nodes[node].Parent;
				for (size_t steps = 0; up >= 0 && static_cast<size_t>(up) < model.Nodes.size() && steps < model.Nodes.size(); steps++)
				{
					auto parentJoint = jointOfNode.find(static_cast<u32>(up));
					if (parentJoint != jointOfNode.end())
					{
						parents[j] = static_cast<i32>(parentJoint->second);
						break;
					}
					matrix = local_matrix(model.Nodes[static_cast<size_t>(up)]) * matrix;
					up = model.Nodes[static_cast<size_t>(up)].Parent;
				}
				pre[j] = matrix;
			}

			// parents first: a walk down from the roots that keeps the order of the readers among siblings
			std::vector<std::vector<size_t>> children(count);
			std::vector<size_t> roots;
			for (size_t j = 0; j < count; j++)
			{
				if (parents[j] < 0 || static_cast<size_t>(parents[j]) == j)
				{
					parents[j] = -1;
					roots.push_back(j);
				}
				else
				{
					children[static_cast<size_t>(parents[j])].push_back(j);
				}
			}
			std::vector<size_t> order;
			std::vector<size_t> stack(roots.rbegin(), roots.rend());
			while (!stack.empty())
			{
				const size_t j = stack.back();
				stack.pop_back();
				order.push_back(j);
				stack.insert(stack.end(), children[j].rbegin(), children[j].rend());
			}
			if (order.size() < count)
			{
				// a loop of parents cannot happen in a node tree, but a corrupt model must not give a skeleton the cooker refuses
				for (size_t j = 0; j < count; j++)
				{
					if (std::find(order.begin(), order.end(), j) == order.end())
					{
						parents[j] = -1;
						order.push_back(j);
					}
				}
			}

			std::vector<u32> newIndexOfOld(count);
			for (size_t n = 0; n < count; n++)
			{
				newIndexOfOld[order[n]] = static_cast<u32>(n);
			}

			ImportedSkeleton result;
			result.Name = skeleton.Name;
			for (size_t n = 0; n < count; n++)
			{
				const size_t j = order[n];
				const u32 node = skeleton.Joints[j];
				result.Joints.push_back(node);
				result.InverseBind.push_back(skeleton.InverseBind[j]);
				result.JointNames.push_back(node < model.Nodes.size() ? model.Nodes[node].Name : std::string{});
				result.Parents.push_back(parents[j] < 0 ? -1 : static_cast<i32>(newIndexOfOld[static_cast<size_t>(parents[j])]));
				result.Pre.push_back(pre[j]);
				glm::vec3 translation, scale;
				glm::quat rotation;
				decompose(node < model.Nodes.size() ? local_matrix(model.Nodes[node]) : glm::mat4(1.0f), translation, rotation, scale);
				result.RestTranslation.push_back(translation);
				result.RestRotation.push_back(rotation);
				result.RestScale.push_back(scale);
			}
			// joint names are unique inside a skeleton (a clip finds its joints by name): a repeated or empty name gets a number
			std::unordered_set<std::string> usedNames;
			for (size_t n = 0; n < result.JointNames.size(); n++)
			{
				std::string& name = result.JointNames[n];
				const std::string base = name.empty() ? "Joint" : name;
				std::string candidate = name.empty() ? "Joint " + std::to_string(n) : name;
				for (int number = 2; usedNames.contains(candidate); number++)
				{
					candidate = base + "_" + std::to_string(number);
				}
				if (candidate != name)
				{
					model.Warnings.push_back("The skeleton " + result.Name + " has a joint name used twice (or empty); " + (name.empty() ? std::string("an empty name") : name) + " became " + candidate);
				}
				usedNames.insert(candidate);
				name = candidate;
			}
			skeleton = std::move(result);

			// the vertices name joints by their place in the skeleton
			for (size_t node = 0; node < model.Nodes.size() && node < model.NodeSkin.size(); node++)
			{
				if (model.NodeSkin[node] != static_cast<int>(index) || node >= model.NodeMesh.size() || model.NodeMesh[node] < 0)
				{
					continue;
				}
				const size_t mesh = static_cast<size_t>(model.NodeMesh[node]);
				if (mesh >= model.Meshes.size() || meshRemappedBy[mesh] == static_cast<int>(index))
				{
					continue;
				}
				if (meshRemappedBy[mesh] >= 0)
				{
					model.Warnings.push_back("The mesh " + model.Meshes[mesh].Data.Name + " is skinned by two skeletons; only the first is used");
					continue;
				}
				meshRemappedBy[mesh] = static_cast<int>(index);
				for (MeshSkinVertex& vertex : model.Meshes[mesh].Data.SkinVertices)
				{
					for (int k = 0; k < 4; k++)
					{
						vertex.Joints[k] = vertex.Joints[k] < count ? newIndexOfOld[vertex.Joints[k]] : 0u;
					}
				}
			}
		}
	}

	void finalize_model_skeletons(ImportedModel& model)
	{
		std::vector<int> meshRemappedBy(model.Meshes.size(), -1);
		for (size_t i = 0; i < model.Skeletons.size(); i++)
		{
			finalize_skeleton(model, i, meshRemappedBy);
		}
	}

	void add_attachment_sockets(const ImportedModel& model, std::vector<PrefabNode>& nodes)
	{
		// the skeleton the Animator of the instance gets: that of the first skinned node
		int skin = -1;
		for (size_t i = 0; i < model.NodeSkin.size(); i++)
		{
			if (model.NodeSkin[i] >= 0 && static_cast<size_t>(model.NodeSkin[i]) < model.Skeletons.size())
			{
				skin = model.NodeSkin[i];
				break;
			}
		}
		if (skin < 0)
		{
			return;
		}
		const ImportedSkeleton& skeleton = model.Skeletons[static_cast<size_t>(skin)];
		std::unordered_map<u32, size_t> jointOfNode;
		for (size_t j = 0; j < skeleton.Joints.size() && j < skeleton.JointNames.size(); j++)
		{
			jointOfNode.emplace(skeleton.Joints[j], j);
		}

		const auto is_rigid_mesh = [&](size_t node)
		{
			return node < model.NodeMesh.size() && node < model.NodeSkin.size() && model.NodeMesh[node] >= 0 && model.NodeSkin[node] < 0;
		};

		for (size_t i = 0; i < nodes.size() && i < model.Nodes.size(); i++)
		{
			if (!is_rigid_mesh(i) || jointOfNode.contains(static_cast<u32>(i)))
			{
				continue;
			}

			// up to the nearest joint, through nodes that stay where the rest pose puts them; a mesh on the way gets the socket instead
			glm::mat4 offset = local_matrix(model.Nodes[i]);
			i32 up = model.Nodes[i].Parent;
			i32 joint = -1;
			bool carried = false;
			for (size_t steps = 0; up >= 0 && static_cast<size_t>(up) < model.Nodes.size() && steps < model.Nodes.size(); steps++)
			{
				const auto found = jointOfNode.find(static_cast<u32>(up));
				if (found != jointOfNode.end())
				{
					joint = static_cast<i32>(found->second);
					break;
				}
				if (is_rigid_mesh(static_cast<size_t>(up)))
				{
					carried = true;
					break;
				}
				offset = local_matrix(model.Nodes[static_cast<size_t>(up)]) * offset;
				up = model.Nodes[static_cast<size_t>(up)].Parent;
			}
			if (joint < 0 || carried)
			{
				continue;
			}

			glm::vec3 translation, scale;
			glm::quat rotation;
			decompose(offset, translation, rotation, scale);
			nodes[i].Components.push_back(ComponentRecord{ "AnimationSocket", {
				FieldRecord{ "Joint", FieldValue(skeleton.JointNames[static_cast<size_t>(joint)]) },
				FieldRecord{ "OffsetPosition", FieldValue(translation) },
				FieldRecord{ "OffsetRotation", FieldValue(rotation) },
				FieldRecord{ "OffsetScale", FieldValue(scale) } } });
		}
	}

	void merge_animation_events(ImportedModel& model, const nlohmann::json& settings)
	{
		const auto list = settings.find("animationEvents");
		if (list == settings.end() || !list->is_array())
		{
			return;
		}

		for (const nlohmann::json& entry : *list)
		{
			if (!entry.is_object() || !entry.contains("clip") || !entry["clip"].is_string() || !entry.contains("time") || !entry["time"].is_number()
				|| !entry.contains("name") || !entry["name"].is_string())
			{
				model.Warnings.push_back("An animation event in the import settings is not {clip, time, name} and was ignored");
				continue;
			}

			const std::string clipName = entry["clip"].get<std::string>();
			auto clip = std::find_if(model.Animations.begin(), model.Animations.end(), [&](const AnimationClipData& c) { return c.Name == clipName; });
			if (clip == model.Animations.end())
			{
				model.Warnings.push_back("The animation event " + entry["name"].get<std::string>() + " names the clip " + clipName + ", which the model does not have");
				continue;
			}
			clip->Events.push_back(AnimationEvent{ entry["time"].get<float>(), entry["name"].get<std::string>() });
		}

		for (AnimationClipData& clip : model.Animations)
		{
			std::stable_sort(clip.Events.begin(), clip.Events.end(), [](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
		}
	}

	AnimationClipData make_named_clip(const ImportedClip& clip, const std::vector<ImportedSkeleton>& skeletons, int& skeletonIndex, std::vector<std::string>& warnings)
	{
		// the skeleton that has the most of the nodes the clip animates (the first one on a tie)
		skeletonIndex = -1;
		size_t best = 0;
		for (size_t s = 0; s < skeletons.size(); s++)
		{
			const std::unordered_set<u32> nodes(skeletons[s].Joints.begin(), skeletons[s].Joints.end());
			size_t covered = 0;
			for (size_t c = 0; c < clip.Channels.size() && c < clip.ChannelNodes.size(); c++)
			{
				covered += (clip.Channels[c].Path != AnimationPath::Weights && nodes.contains(clip.ChannelNodes[c])) ? 1 : 0;
			}
			if (covered > best)
			{
				best = covered;
				skeletonIndex = static_cast<int>(s);
			}
		}

		AnimationClipData result;
		result.Name = clip.Name;
		result.Duration = clip.Duration;
		result.Events = clip.Events;

		std::unordered_map<u32, size_t> jointOfNode;
		if (skeletonIndex >= 0)
		{
			const ImportedSkeleton& skeleton = skeletons[static_cast<size_t>(skeletonIndex)];
			for (size_t j = 0; j < skeleton.Joints.size(); j++)
			{
				jointOfNode.emplace(skeleton.Joints[j], j);
			}
		}

		size_t dropped = 0;
		for (size_t c = 0; c < clip.Channels.size(); c++)
		{
			const auto joint = c < clip.ChannelNodes.size() ? jointOfNode.find(clip.ChannelNodes[c]) : jointOfNode.end();
			if (joint == jointOfNode.end() || clip.Channels[c].Path == AnimationPath::Weights || clip.Channels[c].Times.empty())
			{
				dropped++;
				continue;
			}
			AnimationChannel channel = clip.Channels[c];
			channel.Joint = skeletons[static_cast<size_t>(skeletonIndex)].JointNames[joint->second];
			result.Channels.push_back(std::move(channel));
		}
		if (dropped > 0)
		{
			warnings.push_back("The clip " + clip.Name + ": " + std::to_string(dropped) + " channel(s) animate nodes that are no joint of a skeleton (or morph weights) and were left out");
		}
		return result;
	}
}
