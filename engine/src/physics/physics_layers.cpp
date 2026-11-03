#include <pch.h>
#include "physics_layers.h"

namespace wiwa
{
	PhysicsLayers::PhysicsLayers()
	{
		Names[0] = "Default";
		Matrix.fill(0xFFFFFFFFu);
	}

	bool PhysicsLayers::collides(u32 a, u32 b) const
	{
		if (a >= PHYSICS_LAYER_COUNT || b >= PHYSICS_LAYER_COUNT)
		{
			return false;
		}
		return ((Matrix[a] >> b) & 1u) != 0 && ((Matrix[b] >> a) & 1u) != 0;
	}

	void PhysicsLayers::set_collides(u32 a, u32 b, bool value)
	{
		if (a >= PHYSICS_LAYER_COUNT || b >= PHYSICS_LAYER_COUNT)
		{
			return;
		}
		auto set = [&](u32 from, u32 to)
		{
			if (value)
			{
				Matrix[from] |= 1u << to;
			}
			else
			{
				Matrix[from] &= ~(1u << to);
			}
		};
		set(a, b);
		set(b, a);
	}
}
