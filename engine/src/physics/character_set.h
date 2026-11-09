#pragma once

#include "jolt_config.h"
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <functional>
#include <unordered_map>
#include <vector>

#include "physics_commands.h"
#include "physics_snapshot.h"

namespace wiwa
{
	// The characters of one physics world (JPH::CharacterVirtual, see the CharacterController component), next to its body table. Physics
	// thread only. A character is named by the entity that owns it. Each step it keeps the horizontal velocity it was told to, adds gravity
	// (and the velocity of what it stands on, and a jump when it stands on something), and moves by collide and slide with stairs and
	// stick to floor. Up is +Y. It has an inner rigid body (kinematic, keyed by the character entity), so that queries, triggers and
	// bodies that hit it see it.
	class CharacterSet
	{
	public:
		// The entity of a Jolt body (a null key when it is not one of an entity body).
		using KeyLookup = std::function<BodyKey(JPH::BodyID)>;
		// Tells the world about the inner body of a character that was made or is going: so that events and queries name its entity.
		using BodyHook = std::function<void(JPH::BodyID, BodyKey, bool made)>;

		CharacterSet(JPH::PhysicsSystem& system, KeyLookup key_of, BodyHook on_body) : m_System(system), m_KeyOf(std::move(key_of)), m_OnBody(std::move(on_body)) {}
		~CharacterSet() { clear(); }

		CharacterSet(const CharacterSet&) = delete;
		CharacterSet& operator=(const CharacterSet&) = delete;

		void create(BodyKey key, const CharacterDesc& desc);
		void destroy(BodyKey key);
		void set_velocity(BodyKey key, glm::vec3 velocity);
		void jump(BodyKey key, float speed);
		void teleport(BodyKey key, glm::vec3 position);

		// One fixed step, before the bodies step.
		void step(float dt, JPH::TempAllocator& allocator);

		void write(PhysicsStepSnapshot& snapshot) const;
		void clear();

		u32 count() const { return static_cast<u32>(m_Characters.size()); }
		// The key of the character whose inner body this is (a null key: not one).
		BodyKey key_of_inner(JPH::BodyID id) const;

		// For the query snapshot and the debug drawing: where each character is, its capsule and its layer.
		struct View
		{
			BodyKey Key{};
			glm::vec3 Position{ 0.0f };      // of the capsule shape: its centre
			float Radius = 0.0f;
			float HalfCylinder = 0.0f;       // half the height of the straight part
			i32 Layer = 0;
			GroundState Ground = GroundState::Airborne;
			JPH::RefConst<JPH::Shape> Shape{};
		};
		std::vector<View> views() const;

	private:
		struct Entry
		{
			CharacterDesc Desc{};
			JPH::Ref<JPH::CharacterVirtual> Character{};
			JPH::RefConst<JPH::Shape> Shape{};
			glm::vec3 Desired{ 0.0f };
			float PendingJump = 0.0f;
			glm::vec3 Moved{ 0.0f };         // the velocity it actually had in the last step: its displacement over the step
		};

		JPH::PhysicsSystem& m_System;
		KeyLookup m_KeyOf;
		BodyHook m_OnBody;
		std::unordered_map<BodyKey, Entry> m_Characters{};
	};
}
