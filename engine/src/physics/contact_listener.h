#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "physics_snapshot.h"

namespace wiwa
{
	// Turns Jolt's contact callbacks into PhysicsEvents (see PhysicsEvent for what they mean). The callbacks come from Jolt's job threads
	// during a step, the rest is called from the physics thread between steps. Bodies are registered by the world when it makes them.
	class ContactCollector final : public JPH::ContactListener
	{
	public:
		void set_bodies(JPH::BodyInterface* bodies) { m_Bodies = bodies; }

		// Pairs of bodies (BodyID index and sequence numbers, the smaller first) that never collide: the two bodies of a joint with
		// CollideConnected off. Owned by the joint table; read from Jolt job threads during a step, changed only between steps.
		using NoCollidePairs = std::set<std::pair<JPH::uint32, JPH::uint32>>;
		void set_no_collide(const NoCollidePairs* pairs) { m_NoCollide = pairs; }

		// Adds an event of the step being finished (a joint that broke); it is sorted with the other events of the step.
		void add_event(const PhysicsEvent& event);

		// The entity a registered body belongs to (a null key for one that is not registered).
		BodyKey key_of(JPH::BodyID id) const { const BodyInfo* found = info(id); return found != nullptr ? found->Key : BodyKey{}; }

		void register_body(JPH::BodyID id, BodyKey key, bool sensor, bool reportContacts);
		// The body is going away: its pairs end with an Exit now (the events go to the commands' group), and Jolt's later callbacks for
		// its ID are ignored.
		void unregister_body(JPH::BodyID id);

		void begin_step();
		// After the step: a pair whose last contact is gone gives its Exit, unless a dynamic body of it fell asleep (Jolt reports that as
		// a removed contact, but the bodies still touch).
		void end_step();

		// Moves the events made since the last call into `events`: the commands' first, then the step's, each sorted.
		void take_events(std::vector<PhysicsEvent>& events);

		JPH::ValidateResult OnContactValidate(const JPH::Body& body1, const JPH::Body& body2, JPH::RVec3Arg baseOffset, const JPH::CollideShapeResult& collisionResult) override;
		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override;
		void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override;
		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

	private:
		struct BodyInfo
		{
			BodyKey Key{};
			bool Sensor = false;
			bool Report = false;
		};

		using PairKey = std::pair<BodyKey, BodyKey>; // first < second

		struct PairState
		{
			JPH::BodyID IdA{};
			JPH::BodyID IdB{};
			bool Trigger = false;
			bool Report = false;
			std::vector<JPH::SubShapeIDPair> Contacts{}; // an empty list: the pair is held (asleep) or ended this step
			u64 EnteredStep = 0;
			u64 StayedStep = 0;
		};

		const BodyInfo* info(JPH::BodyID id) const;
		void contact(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, bool persisted);
		static void sort_events(std::vector<PhysicsEvent>& events);

		JPH::BodyInterface* m_Bodies = nullptr;
		const NoCollidePairs* m_NoCollide = nullptr;
		std::unordered_map<JPH::uint32, BodyInfo> m_Infos{}; // by BodyID index and sequence; changed only between steps

		std::mutex m_Mutex{};
		std::map<PairKey, PairState> m_Pairs{};
		std::vector<PhysicsEvent> m_CommandEvents{};
		std::vector<PhysicsEvent> m_StepEvents{};
		u64 m_Step = 0;
	};
}
