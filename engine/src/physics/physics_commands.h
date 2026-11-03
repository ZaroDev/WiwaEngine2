#pragma once

#include <variant>

#include "physics_types.h"

namespace wiwa
{
	// Replaces the step settings (rate, gravity, iterations, sleep threshold) from the next step on.
	struct SetSettings
	{
		PhysicsSettings Settings{};
	};

	// Plain-data commands from the main thread to the physics thread, applied at the start of the next step in submission order.
	// New kinds (CreateBody, DestroyBody, ...) are added as new alternatives.
	using PhysicsCommand = std::variant<SetSettings>;
}
