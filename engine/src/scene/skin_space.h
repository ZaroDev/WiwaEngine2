#pragma once

#include <glm/glm.hpp>
#include <scene/components.h>

// A skinned mesh, a socket and joint_world stand in the frame of their Animator: the pose places the vertices, so the nodes between them and
// the Animator are ignored, the model's import scale with them. The importer records that scale in the prefab (PrefabData::ImportScale), the
// prefab expander copies it to Animator::ModelScale, and everything that stands in the Animator's frame applies it with this matrix.
namespace wiwa
{
	inline glm::mat4 model_scale_matrix(const Animator& animator)
	{
		const float scale = animator.ModelScale > 0.0f ? animator.ModelScale : 1.0f;
		return scale == 1.0f ? glm::mat4(1.0f) : glm::scale(glm::mat4(1.0f), glm::vec3(scale));
	}
}
