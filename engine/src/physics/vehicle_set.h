#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/PhysicsSystem.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "physics_commands.h"
#include "physics_snapshot.h"

namespace wiwa
{
	struct BuiltVehicle;

	// The vehicles of one physics world (vehicles.md), next to its body, joint and character tables. Physics thread only. A vehicle is named
	// by the entity of its body and is one JPH::VehicleConstraint with a WheeledVehicleController; grip comes from our tire model
	// (tire_model.h) through the controller's tire impulse callback and the constraint's own friction combine function (the world's combine
	// functions are not touched). A vehicle waits, unmade, while its body is missing or not dynamic.
	class VehicleSet
	{
	public:
		using BodyLookup = std::function<JPH::BodyID(BodyKey)>;
		using KeyLookup = std::function<BodyKey(JPH::BodyID)>;

		VehicleSet(JPH::PhysicsSystem& system, BodyLookup body_of, KeyLookup key_of);
		~VehicleSet();

		VehicleSet(const VehicleSet&) = delete;
		VehicleSet& operator=(const VehicleSet&) = delete;

		void create(BodyKey body, const VehicleDesc& desc);
		void destroy(BodyKey body);
		void set_input(BodyKey body, const VehicleInput& input);
		void shift(BodyKey body, i32 direction);
		void set_assist(BodyKey body, VehicleAssist assist, float strength);

		// The body of a vehicle is going: the vehicle is unmade and waits for a body again. Call before the body is removed.
		void body_destroyed(BodyKey key);

		// One fixed step, before PhysicsSystem::Update: makes waiting vehicles whose body is there, gives each its input.
		void step(float dt);

		void write(PhysicsStepSnapshot& snapshot) const;
		void clear();

		u32 made_count() const;

	private:
		struct Entry
		{
			VehicleDesc Desc{};
			bool Unbuildable = false; // logged once; waits for another CreateVehicle
			std::unique_ptr<BuiltVehicle> Built{};
		};
		// What the driver and the scripts set for a body: kept apart from the vehicle, so an input sent before the vehicle is made (its asset
		// still loading) or across a remake (disabled and enabled again) still counts. Forgotten only with the world (clear).
		struct Controls
		{
			VehicleInput Input{};
			std::array<float, static_cast<size_t>(VehicleAssist::Count)> Assists{ -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
		};

		void make(BodyKey key, Entry& entry);
		void unmake(Entry& entry);

		JPH::PhysicsSystem& m_System;
		BodyLookup m_BodyOf;
		KeyLookup m_KeyOf;
		std::map<BodyKey, Entry> m_Vehicles{}; // ordered: the same commands step the vehicles in the same order
		std::map<BodyKey, Controls> m_Controls{};
		float m_LastDt = 1.0f / 60.0f;
	};
}
