#include <pch.h>
#include "pose.h"

namespace wiwa
{
	Pose rest_pose(const SkeletonData& skeleton)
	{
		Pose pose;
		const size_t count = skeleton.JointNames.size();
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
			if (j < skeleton.Pre.size())
			{
				local = skeleton.Pre[j] * local;
			}
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

	void add_pose(const Pose& base, const Pose& current, const Pose& reference, float weight, const std::vector<float>* mask, Pose& out)
	{
		const size_t count = std::min({ base.size(), current.size(), reference.size() });
		Pose result = base;
		result.T.resize(count);
		result.R.resize(count);
		result.S.resize(count);
		for (size_t j = 0; j < count; j++)
		{
			const float w = std::clamp(weight * (mask != nullptr && j < mask->size() ? (*mask)[j] : 1.0f), 0.0f, 1.0f);
			result.T[j] += w * (current.T[j] - reference.T[j]);
			result.S[j] *= glm::mix(glm::vec3(1.0f), current.S[j] / glm::max(glm::abs(reference.S[j]), glm::vec3(1e-6f)), w);

			glm::quat delta = current.R[j] * glm::inverse(reference.R[j]);
			if (delta.w < 0.0f)
			{
				delta = -delta;
			}
			const glm::quat partial = glm::normalize(glm::quat(glm::mix(1.0f, delta.w, w), delta.x * w, delta.y * w, delta.z * w));
			result.R[j] = glm::normalize(partial * result.R[j]);
		}
		out = std::move(result);
	}

	void decompose_matrix(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
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
}
