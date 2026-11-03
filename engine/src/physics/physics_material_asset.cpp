#include <pch.h>
#include "physics_material_asset.h"

#include <algorithm>
#include <cmath>

#include <assets/wasset_io.h>

namespace wiwa
{
	PhysicsMaterialData default_physics_material()
	{
		return PhysicsMaterialData{};
	}

	bool physics_material_valid(const PhysicsMaterialData& data)
	{
		const auto ok = [](float v) { return std::isfinite(v) && v >= 0.0f; };
		return ok(data.Friction) && ok(data.Restitution) && ok(data.Density)
			&& static_cast<u32>(data.FrictionCombine) <= static_cast<u32>(CombineMode::Multiply)
			&& static_cast<u32>(data.RestitutionCombine) <= static_cast<u32>(CombineMode::Multiply);
	}

	CombineMode stronger_combine_mode(CombineMode a, CombineMode b)
	{
		// the enum order is Average, Min, Max, Multiply: the higher value has the higher priority except that Max beats Min, which
		// is already the case (Min = 1, Max = 2)
		return static_cast<u32>(a) >= static_cast<u32>(b) ? a : b;
	}

	float combine_values(CombineMode mode, float a, float b)
	{
		switch (mode)
		{
		case CombineMode::Min: return std::min(a, b);
		case CombineMode::Max: return std::max(a, b);
		case CombineMode::Multiply: return a * b;
		default: return 0.5f * (a + b);
		}
	}

	void archive(BinaryArchive& ar, PhysicsMaterialData& v)
	{
		u32 version = PHYSICS_MATERIAL_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != PHYSICS_MATERIAL_VERSION)
		{
			ar.fail();
			return;
		}

		ar.value(v.Friction);
		ar.value(v.Restitution);
		ar.value(v.Density);
		ar.value(v.FrictionCombine);
		ar.value(v.RestitutionCombine);

		if (ar.reading() && ar.ok() && !physics_material_valid(v))
		{
			ar.fail();
		}
	}
}
