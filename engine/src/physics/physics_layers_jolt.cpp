#include <pch.h>
#include "physics_layers_jolt.h"

#include <algorithm>

namespace wiwa
{
	namespace
	{
		constexpr u32 kCategoryBits = 2;
		constexpr u32 kCategoryMask = (1u << kCategoryBits) - 1;
		static_assert(static_cast<u32>(BodyCategory::Count) <= (1u << kCategoryBits));
	}

	JPH::ObjectLayer to_object_layer(i32 layer, BodyCategory category)
	{
		const u32 index = static_cast<u32>(std::clamp<i32>(layer, 0, static_cast<i32>(PHYSICS_LAYER_COUNT) - 1));
		return static_cast<JPH::ObjectLayer>((index << kCategoryBits) | static_cast<u32>(category));
	}

	u32 layer_index(JPH::ObjectLayer layer) { return static_cast<u32>(layer) >> kCategoryBits; }
	BodyCategory layer_category(JPH::ObjectLayer layer) { return static_cast<BodyCategory>(static_cast<u32>(layer) & kCategoryMask); }

	JPH::uint BroadPhaseLayers::GetNumBroadPhaseLayers() const { return static_cast<JPH::uint>(BodyCategory::Count); }

	JPH::BroadPhaseLayer BroadPhaseLayers::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
	{
		return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer_category(layer)));
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* BroadPhaseLayers::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
	{
		switch (static_cast<BodyCategory>(static_cast<JPH::BroadPhaseLayer::Type>(layer)))
		{
		case BodyCategory::Static: return "static";
		case BodyCategory::Moving: return "moving";
		case BodyCategory::Trigger: return "trigger";
		default: return "none";
		}
	}
#endif

	bool ObjectVsBroadPhase::ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const
	{
		const BodyCategory mine = layer_category(layer);
		const BodyCategory theirs = static_cast<BodyCategory>(static_cast<JPH::BroadPhaseLayer::Type>(broad));
		if (mine == BodyCategory::NoCollision || theirs == BodyCategory::NoCollision)
		{
			return false;
		}
		return !(mine == BodyCategory::Static && theirs == BodyCategory::Static);
	}

	bool ObjectLayerPairs::ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const
	{
		const BodyCategory ca = layer_category(a);
		const BodyCategory cb = layer_category(b);
		if (ca == BodyCategory::NoCollision || cb == BodyCategory::NoCollision || (ca == BodyCategory::Static && cb == BodyCategory::Static))
		{
			return false;
		}
		return m_Layers.collides(layer_index(a), layer_index(b));
	}
}
