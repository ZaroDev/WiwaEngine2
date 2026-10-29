#pragma once

#include <scene/scene_editor.h>

namespace wiwa
{
	enum class GizmoOperation { Translate, Rotate, Scale };

	// The matrix the transform gizmo (ImGuizmo) is given for an entity: its world matrix, except that an axis with no length (a scale of 0)
	// gets a unit axis, so the gizmo still has a position and an orientation to show. A zero axis is completed from the other two (X = Y x Z)
	// when they allow it, else it is the world axis. A matrix that is not finite is replaced by the identity.
	glm::mat4 gizmo_display_matrix(const glm::mat4& world);

	// Writes what the gizmo did to the entity's local Transform: `before` is the display matrix it was given, `after` what it handed back (in the
	// world). Translate sets Position, Rotate sets Rotation, Scale sets Scale (each axis by the factor the gizmo scaled it, from at least 0.01
	// so that an entity whose scale was zeroed can be scaled up again); nothing else changes. The world is turned into the entity's space
	// through its parent. Returns false, and writes nothing, when there was no change, the entity is gone or the numbers cannot be used
	// (not finite, a parent that has no scale and so cannot be undone).
	bool apply_gizmo_result(SceneEditor& editor, entt::entity entity, GizmoOperation operation, const glm::mat4& before, const glm::mat4& after);
}
