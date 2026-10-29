#include <editor_common.h>
#include "gizmo_apply.h"

#include <cmath>

#include <scene/components.h>

namespace wiwa
{
	namespace
	{
		constexpr float MIN_AXIS_LENGTH = 1e-4f;
		constexpr float MIN_SCALE = 0.01f;  // what a scale recovers from
		constexpr float MAX_SCALE = 1e6f;

		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		bool finite(const glm::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

		bool finite(const glm::mat4& m)
		{
			return finite(glm::vec3(m[0])) && finite(glm::vec3(m[1])) && finite(glm::vec3(m[2])) && finite(glm::vec3(m[3])) && std::isfinite(m[0][3]) && std::isfinite(m[1][3])
				&& std::isfinite(m[2][3]) && std::isfinite(m[3][3]);
		}

		bool invertible(const glm::mat4& m)
		{
			const float determinant = glm::determinant(m);
			return std::isfinite(determinant) && std::abs(determinant) > 1e-12f;
		}

		glm::quat rotation_of(const glm::mat4& matrix)
		{
			glm::vec3 axes[3];
			const glm::vec3 fallback[3] = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
			for (int i = 0; i < 3; i++)
			{
				const glm::vec3 column(matrix[i]);
				const float length = glm::length(column);
				axes[i] = finite(column) && length > MIN_AXIS_LENGTH ? column / length : fallback[i];
			}
			const glm::quat q = glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2]));
			return finite(q) ? glm::normalize(q) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	glm::mat4 gizmo_display_matrix(const glm::mat4& world)
	{
		if (!finite(world))
		{
			return glm::mat4(1.0f);
		}

		bool missing[3];
		int missingCount = 0;
		for (int i = 0; i < 3; i++)
		{
			missing[i] = glm::length(glm::vec3(world[i])) <= MIN_AXIS_LENGTH;
			missingCount += missing[i] ? 1 : 0;
		}
		if (missingCount == 0)
		{
			return world;
		}

		glm::mat4 shown = world;
		const glm::vec3 fallback[3] = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
		for (int i = 0; i < 3; i++)
		{
			if (!missing[i])
			{
				continue;
			}

			glm::vec3 axis = fallback[i];
			if (missingCount == 1)
			{
				// right handed: X = Y x Z, Y = Z x X, Z = X x Y
				const glm::vec3 completed = glm::cross(glm::vec3(world[(i + 1) % 3]), glm::vec3(world[(i + 2) % 3]));
				const float length = glm::length(completed);
				if (std::isfinite(length) && length > MIN_AXIS_LENGTH)
				{
					axis = completed / length;
				}
			}
			shown[i] = glm::vec4(axis, 0.0f);
		}
		return shown;
	}

	bool apply_gizmo_result(SceneEditor& editor, entt::entity entity, GizmoOperation operation, const glm::mat4& before, const glm::mat4& after)
	{
		entt::registry& registry = editor.scene().registry();
		if (!registry.valid(entity) || !registry.all_of<Transform, Hierarchy>(entity) || !finite(before) || !finite(after))
		{
			return false;
		}

		glm::mat4 parentWorld(1.0f);
		const entt::entity parent = registry.get<Hierarchy>(entity).Parent;
		if (parent != entt::null && registry.valid(parent) && registry.all_of<WorldTransform>(parent))
		{
			parentWorld = registry.get<WorldTransform>(parent).Matrix;
		}

		const Transform current = registry.get<Transform>(entity);
		switch (operation)
		{
		case GizmoOperation::Translate:
		{
			if (!invertible(parentWorld))
			{
				return false;
			}
			const glm::vec3 local = glm::vec3(glm::inverse(parentWorld) * glm::vec4(glm::vec3(after[3]), 1.0f));
			if (!finite(local) || local == current.Position)
			{
				return false;
			}
			return editor.set_field(entity, "Transform", "Position", FieldValue(local));
		}

		case GizmoOperation::Rotate:
		{
			const glm::quat local = glm::normalize(glm::inverse(rotation_of(parentWorld)) * rotation_of(after));
			if (!finite(local) || glm::dot(local, current.Rotation) > 0.999999f)
			{
				return false;
			}
			return editor.set_field(entity, "Transform", "Rotation", FieldValue(local));
		}

		case GizmoOperation::Scale:
		{
			glm::vec3 scale = current.Scale;
			for (int i = 0; i < 3; i++)
			{
				const float was = glm::length(glm::vec3(before[i]));
				const float now = glm::length(glm::vec3(after[i]));
				if (!std::isfinite(was) || !std::isfinite(now) || was <= MIN_AXIS_LENGTH)
				{
					continue;
				}
				const float sign = current.Scale[i] < 0.0f ? -1.0f : 1.0f;
				scale[i] = sign * std::min(std::max(std::abs(current.Scale[i]), MIN_SCALE) * (now / was), MAX_SCALE);
			}
			if (!finite(scale) || scale == current.Scale)
			{
				return false;
			}
			return editor.set_field(entity, "Transform", "Scale", FieldValue(scale));
		}
		}
		return false;
	}
}
