#pragma once

#include <string_view>
#include <vector>

#include <assets/asset_data.h>
#include <math/math.h>

namespace wiwa
{
	// The local transform of every joint of a skeleton, indexed by joint. Parents come before their children, so one forward pass makes
	// the model space matrices.
	struct Pose
	{
		std::vector<glm::vec3> T{};
		std::vector<glm::quat> R{};
		std::vector<glm::vec3> S{};

		size_t size() const { return T.size(); }
	};

	// One row-major 3x4 affine matrix as the shaders read it: p' = (dot(Row[0], p1), dot(Row[1], p1), dot(Row[2], p1)) for p1 = (p, 1).
	struct JointMatrix
	{
		glm::vec4 Row[3]{};
	};
	static_assert(sizeof(JointMatrix) == 48);

	Pose rest_pose(const SkeletonData& skeleton);

	void model_matrices(const SkeletonData& skeleton, const Pose& pose, std::vector<glm::mat4>& out);

	// model[j] * inverseBind[j], as JointMatrix rows.
	void build_palette(const SkeletonData& skeleton, const std::vector<glm::mat4>& model, std::vector<JointMatrix>& out);

	// The joint with this name, or -1.
	i32 find_joint(const SkeletonData& skeleton, std::string_view name);

	// 1 for the joint and all its descendants, 0 for the other joints; all 0 for a joint that does not exist.
	std::vector<float> subtree_mask(const SkeletonData& skeleton, i32 joint);

	// out = base plus the change of current from reference (a clip from its first frame), by weight * mask[j]. out may be base.
	void add_pose(const Pose& base, const Pose& current, const Pose& reference, float weight, const std::vector<float>* mask, Pose& out);

	// Splits a matrix made of translation, rotation and scale.
	void decompose_matrix(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale);

	// out = a toward b by weight * mask[j] per joint (translation and scale lerp, rotation nlerp along the shortest path). A null mask is 1 everywhere.
	void blend_poses(const Pose& a, const Pose& b, float weight, const std::vector<float>* mask, Pose& out);
}
