#include <pch.h>
#include "jolt_link_check.h"
#include "jolt_config.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace wiwa
{
	namespace
	{
		constexpr JPH::ObjectLayer kLayerStatic = 0;
		constexpr JPH::ObjectLayer kLayerMoving = 1;
		constexpr JPH::BroadPhaseLayer kBroadStatic(0);
		constexpr JPH::BroadPhaseLayer kBroadMoving(1);

		class CheckBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return layer == kLayerStatic ? kBroadStatic : kBroadMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer == kBroadStatic ? "static" : "moving"; }
#endif
		};

		class CheckObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override { return layer == kLayerMoving || broad == kBroadMoving; }
		};

		class CheckObjectPair final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == kLayerMoving || b == kLayerMoving; }
		};
	}

	bool run_physics_link_check()
	{
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		bool ok = false;
		{
			CheckBroadPhaseLayers broadPhaseLayers;
			CheckObjectVsBroadPhase objectVsBroad;
			CheckObjectPair objectPair;

			JPH::PhysicsSystem system;
			system.Init(64, 0, 64, 64, broadPhaseLayers, objectVsBroad, objectPair);

			JPH::TempAllocatorImpl tempAllocator(1024 * 1024);
			JPH::JobSystemSingleThreaded jobs(JPH::cMaxPhysicsJobs);
			const JPH::EPhysicsUpdateError error = system.Update(1.0f / 60.0f, 1, &tempAllocator, &jobs);
			ok = error == JPH::EPhysicsUpdateError::None && system.GetNumBodies() == 0;
		}

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;

		if (!ok)
			WI_PHYSICS_LOG(Error, "Jolt link check failed");
		return ok;
	}
}
