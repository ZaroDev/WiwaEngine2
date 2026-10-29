#pragma once

#include "math.h"

// Math the editor's overlays need. The transform gizmo itself is ImGuizmo's; what is left here is sizing things so that they keep their size on
// screen. Returns finite values for input that cannot be used (NaN, a zero size).
namespace wiwa::gizmo
{
	// The world size that covers `pixels` pixels of the viewport's height at the position, so a marker keeps its size on screen. Works for
	// perspective and orthographic projections; a position behind or at the camera uses its distance, never zero. 1 when the input cannot be used.
	float screen_constant_scale(glm::vec3 worldPosition, const glm::mat4& view, const glm::mat4& projection, float pixels, float viewportHeight);
}
