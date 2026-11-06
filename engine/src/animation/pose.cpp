#include <pch.h>
#include "pose.h"

namespace wiwa
{
	Pose rest_pose(const SkeletonData& skeleton)
	{
		Pose pose;
		const size_t count = skeleton.Joints.size();
		pose.T = skeleton.RestTranslation;
		pose.R = skeleton.RestRotation;
		pose.S = skeleton.RestScale;
		pose.T.resize(count, glm::vec3(0.0f));
		pose.R.resize(count, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		pose.S.resize(count, glm::vec3(1.0f));
		return pose;
	}

	void model_matrices(const SkeletonData& skeleton, const Pose& pose, std::vector<glm::mat4>& out)
	{
		const size_t count = std::min(pose.size(), skeleton.Parents.size());
		out.resize(count);
		for (size_t j = 0; j < count; j++)
		{
			glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.T[j]) * glm::mat4_cast(pose.R[j]) * glm::scale(glm::mat4(1.0f), pose.S[j]);
			const i32 parent = skeleton.Parents[j];
			out[j] = (parent >= 0 && static_cast<size_t>(parent) < j) ? out[static_cast<size_t>(parent)] * local : local;
		}
	}

	void build_palette(const SkeletonData& skeleton, const std::vector<glm::mat4>& model, std::vector<JointMatrix>& out)
	{
		const size_t count = std::min(model.size(), skeleton.InverseBind.size());
		out.resize(count);
		for (size_t j = 0; j < count; j++)
		{
			const glm::mat4 matrix = model[j] * skeleton.InverseBind[j];
			for (int row = 0; row < 3; row++)
			{
				out[j].Row[row] = glm::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
			}
		}
	}

	i32 find_joint(const SkeletonData& skeleton, std::string_view name)
	{
		for (size_t j = 0; j < skeleton.JointNames.size(); j++)
		{
			if (skeleton.JointNames[j] == name)
			{
				return static_cast<i32>(j);
			}
		}
		return -1;
	}

	std::vector<float> subtree_mask(const SkeletonData& skeleton, i32 joint)
	{
		std::vector<float> mask(skeleton.Parents.size(), 0.0f);
		if (joint < 0 || static_cast<size_t>(joint) >= mask.size())
		{
			return mask;
		}
		mask[static_cast<size_t>(joint)] = 1.0f;
		for (size_t j = static_cast<size_t>(joint) + 1; j < mask.size(); j++)
		{
			const i32 parent = skeleton.Parents[j];
			if (parent >= 0 && mask[static_cast<size_t>(parent)] > 0.0f)
			{
				mask[j] = 1.0f;
			}
		}
		return mask;
	}

	void blend_poses(const Pose& a, const Pose& b, float weight, const std::vector<float>* mask, Pose& out)
	{
		const size_t count = std::min(a.size(), b.size());
		out.T.resize(count);
		out.R.resize(count);
		out.S.resize(count);
		for (size_t j = 0; j < count; j++)
		{
			const float w = std::clamp(weight * (mask != nullptr && j < mask->size() ? (*mask)[j] : 1.0f), 0.0f, 1.0f);
			out.T[j] = glm::mix(a.T[j], b.T[j], w);
			out.S[j] = glm::mix(a.S[j], b.S[j], w);
			glm::quat target = b.R[j];
			if (glm::dot(a.R[j], target) < 0.0f)
			{
				target = -target;
			}
			out.R[j] = glm::normalize(glm::quat(
				glm::mix(a.R[j].w, target.w, w), glm::mix(a.R[j].x, target.x, w), glm::mix(a.R[j].y, target.y, w), glm::mix(a.R[j].z, target.z, w)));
		}
	}
}
