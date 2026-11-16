#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <scene/components.h>

namespace wiwa
{
	struct PerceptionGizmoLine
	{
		glm::vec3 A{ 0.0f };
		glm::vec3 B{ 0.0f };
	};

	struct PerceptionGizmoShapes
	{
		std::vector<PerceptionGizmoLine> Sight{};   // the cone: from the eye to its far rim, and the rim
		glm::vec3 Eye{ 0.0f };
		float HearingRadius = 0.0f;                  // a sphere round the entity (0: none)
		glm::vec3 HearingCentre{ 0.0f };
	};

	// What the viewport draws for a selected Perceiver: the sight cone (the eye, the far rim at SightRange, edge lines to it; a full circle shows a
	// sphere's worth of rim) and the hearing radius round the entity. Empty for a perceiver whose world transform is not a finite point.
	PerceptionGizmoShapes perceiver_gizmo_shapes(const Perceiver& perceiver, const glm::mat4& world, u32 rimSegments = 24);
}
