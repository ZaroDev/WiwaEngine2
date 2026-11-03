#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <memory>
#include <vector>

#include "jolt_runtime.h"
#include "query_snapshot.h"

namespace wiwa
{
	// Physics-thread side of the query snapshot (the Jolt types stay out of query_snapshot.h).
	struct QueryBody
	{
		BodyKey Key{};
		JPH::RefConst<JPH::Shape> Shape{};
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		u32 Layer = 0;
	};

	struct QuerySnapshot::Impl
	{
		JoltRuntimeScope Runtime{}; // the shapes and the Jolt calls need it for as long as the snapshot lives
		std::shared_ptr<const std::vector<QueryBody>> Static{}; // shared between steps until a static body changes
		std::vector<QueryBody> Moving{};
	};
}
