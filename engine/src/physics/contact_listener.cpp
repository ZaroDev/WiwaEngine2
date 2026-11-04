#include <pch.h>
#include "contact_listener.h"

#include <algorithm>

#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

#include "physics_material_jolt.h"

namespace wiwa
{
	namespace
	{
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

		bool event_less(const PhysicsEvent& a, const PhysicsEvent& b)
		{
			if (a.Kind != b.Kind)
			{
				return a.Kind < b.Kind;
			}
			return a.A != b.A ? a.A < b.A : a.B < b.B;
		}
	}

	void ContactCollector::register_body(JPH::BodyID id, BodyKey key, bool sensor, bool reportContacts)
	{
		m_Infos[id.GetIndexAndSequenceNumber()] = BodyInfo{ key, sensor, reportContacts };
	}

	void ContactCollector::unregister_body(JPH::BodyID id)
	{
		const auto infoIt = m_Infos.find(id.GetIndexAndSequenceNumber());
		if (infoIt == m_Infos.end())
		{
			return;
		}
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto it = m_Pairs.begin(); it != m_Pairs.end();)
		{
			if (it->second.IdA == id || it->second.IdB == id)
			{
				PhysicsEvent event;
				event.Kind = it->second.Trigger ? PhysicsEventKind::TriggerExit : PhysicsEventKind::CollisionExit;
				event.A = it->first.first;
				event.B = it->first.second;
				m_CommandEvents.push_back(event);
				it = m_Pairs.erase(it);
			}
			else
			{
				++it;
			}
		}
		m_Infos.erase(infoIt);
	}

	void ContactCollector::begin_step()
	{
		m_Step++;
	}

	void ContactCollector::end_step()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto it = m_Pairs.begin(); it != m_Pairs.end();)
		{
			PairState& state = it->second;
			if (!state.Contacts.empty())
			{
				++it;
				continue;
			}
			const auto asleep = [&](JPH::BodyID id)
			{
				return m_Bodies != nullptr && m_Bodies->GetMotionType(id) == JPH::EMotionType::Dynamic && !m_Bodies->IsActive(id);
			};
			if (asleep(state.IdA) || asleep(state.IdB))
			{
				++it; // still touching, only not simulated: the pair ends when the body wakes up and finds nothing
				continue;
			}
			PhysicsEvent event;
			event.Kind = state.Trigger ? PhysicsEventKind::TriggerExit : PhysicsEventKind::CollisionExit;
			event.A = it->first.first;
			event.B = it->first.second;
			m_StepEvents.push_back(event);
			it = m_Pairs.erase(it);
		}
	}

	void ContactCollector::add_event(const PhysicsEvent& event)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_StepEvents.push_back(event);
	}

	JPH::ValidateResult ContactCollector::OnContactValidate(const JPH::Body& body1, const JPH::Body& body2, JPH::RVec3Arg, const JPH::CollideShapeResult&)
	{
		if (m_NoCollide != nullptr && !m_NoCollide->empty())
		{
			const JPH::uint32 first = body1.GetID().GetIndexAndSequenceNumber();
			const JPH::uint32 second = body2.GetID().GetIndexAndSequenceNumber();
			if (m_NoCollide->contains({ std::min(first, second), std::max(first, second) }))
			{
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			}
		}
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void ContactCollector::take_events(std::vector<PhysicsEvent>& events)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		sort_events(m_CommandEvents);
		sort_events(m_StepEvents);
		events.insert(events.end(), m_CommandEvents.begin(), m_CommandEvents.end());
		events.insert(events.end(), m_StepEvents.begin(), m_StepEvents.end());
		m_CommandEvents.clear();
		m_StepEvents.clear();
	}

	void ContactCollector::sort_events(std::vector<PhysicsEvent>& events)
	{
		std::stable_sort(events.begin(), events.end(), event_less);
	}

	const ContactCollector::BodyInfo* ContactCollector::info(JPH::BodyID id) const
	{
		const auto it = m_Infos.find(id.GetIndexAndSequenceNumber());
		return it == m_Infos.end() ? nullptr : &it->second;
	}

	void ContactCollector::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&)
	{
		contact(body1, body2, manifold, false);
	}

	void ContactCollector::OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&)
	{
		contact(body1, body2, manifold, true);
	}

	void ContactCollector::contact(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, bool persisted)
	{
		const BodyInfo* info1 = info(body1.GetID());
		const BodyInfo* info2 = info(body2.GetID());
		if (info1 == nullptr || info2 == nullptr)
		{
			return; // a body that was destroyed
		}

		// A is the smaller key; Jolt's normal points from its body 1 to its body 2
		const bool swap = info2->Key < info1->Key;
		const JPH::Body& bodyA = swap ? body2 : body1;
		const JPH::Body& bodyB = swap ? body1 : body2;
		const BodyInfo& infoA = swap ? *info2 : *info1;
		const BodyInfo& infoB = swap ? *info1 : *info2;
		const bool trigger = infoA.Sensor || infoB.Sensor;
		const bool report = infoA.Report || infoB.Report;
		const JPH::SubShapeIDPair subShapes(body1.GetID(), manifold.mSubShapeID1, body2.GetID(), manifold.mSubShapeID2);

		std::lock_guard<std::mutex> lock(m_Mutex);
		auto [it, created] = m_Pairs.try_emplace(PairKey{ infoA.Key, infoB.Key });
		PairState& state = it->second;
		if (created)
		{
			state.IdA = bodyA.GetID();
			state.IdB = bodyB.GetID();
			state.Trigger = trigger;
			state.Report = report;
		}
		if (std::find(state.Contacts.begin(), state.Contacts.end(), subShapes) == state.Contacts.end())
		{
			state.Contacts.push_back(subShapes);
		}

		PhysicsEvent event;
		event.A = infoA.Key;
		event.B = infoB.Key;
		if (trigger)
		{
			if (created)
			{
				event.Kind = PhysicsEventKind::TriggerEnter;
				m_StepEvents.push_back(event);
			}
			return;
		}
		const bool enter = created;
		const bool stay = !created && persisted && report && state.EnteredStep != m_Step && state.StayedStep != m_Step;
		if (!enter && !stay)
		{
			return;
		}

		JPH::Vec3 sum = JPH::Vec3::sZero();
		for (const JPH::Vec3& point : manifold.mRelativeContactPointsOn1)
		{
			sum += point;
		}
		const JPH::RVec3 point = manifold.mBaseOffset + (manifold.mRelativeContactPointsOn1.empty() ? JPH::Vec3::sZero() : sum / static_cast<float>(manifold.mRelativeContactPointsOn1.size()));
		const JPH::Vec3 normal = swap ? -manifold.mWorldSpaceNormal : manifold.mWorldSpaceNormal;

		JPH::CollisionEstimationResult estimate;
		JPH::EstimateCollisionResponse(body1, body2, manifold, estimate, combine_friction(body1, manifold.mSubShapeID1, body2, manifold.mSubShapeID2),
			combine_restitution(body1, manifold.mSubShapeID1, body2, manifold.mSubShapeID2));
		float impulse = 0.0f;
		for (const float part : estimate.mContactImpulse)
		{
			impulse += part;
		}

		event.Kind = enter ? PhysicsEventKind::CollisionEnter : PhysicsEventKind::CollisionStay;
		event.Point = to_glm(JPH::Vec3(point));
		event.Normal = to_glm(normal);
		event.Impulse = impulse;
		event.RelativeVelocity = to_glm(bodyB.GetPointVelocity(point) - bodyA.GetPointVelocity(point));
		(enter ? state.EnteredStep : state.StayedStep) = m_Step;
		m_StepEvents.push_back(event);
	}

	void ContactCollector::OnContactRemoved(const JPH::SubShapeIDPair& pair)
	{
		const BodyInfo* info1 = info(pair.GetBody1ID());
		const BodyInfo* info2 = info(pair.GetBody2ID());
		if (info1 == nullptr || info2 == nullptr)
		{
			return;
		}
		const PairKey key = info2->Key < info1->Key ? PairKey{ info2->Key, info1->Key } : PairKey{ info1->Key, info2->Key };
		std::lock_guard<std::mutex> lock(m_Mutex);
		const auto it = m_Pairs.find(key);
		if (it != m_Pairs.end())
		{
			std::erase(it->second.Contacts, pair);
		}
	}
}
