#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "physics_layers.h"

namespace wiwa
{
	// How a body sits in the broad phase. NoCollision is the placeholder of a body whose shape could not be made.
	enum class BodyCategory : u32 { Static, Moving, Trigger, NoCollision, Count };

	// A Jolt object layer is a project layer index (0..31, clamped) and a category; the category is the broad phase layer, so static
	// bodies stay in a tree of their own. Two bodies collide when the project layers' matrix says so (and they are not both static, and
	// neither is a placeholder).
	JPH::ObjectLayer to_object_layer(i32 layer, BodyCategory category);
	u32 layer_index(JPH::ObjectLayer layer);
	BodyCategory layer_category(JPH::ObjectLayer layer);

	class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override;
		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
	};

	class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override;
	};

	class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter
	{
	public:
		explicit ObjectLayerPairs(const PhysicsLayers& layers) : m_Layers(layers) {}
		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override;

	private:
		PhysicsLayers m_Layers;
	};
}
