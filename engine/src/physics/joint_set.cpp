#include <pch.h>
#include "joint_set.h"

#include "joint_builder.h"

#include <algorithm>

namespace wiwa
{
	void JointSet::create(const JointDesc& desc)
	{
		if (!desc.A.valid())
		{
			return;
		}
		if (const auto it = m_Joints.find(desc.A); it != m_Joints.end())
		{
			unmake(it->second);
		}
		Entry& entry = m_Joints[desc.A];
		entry = Entry{};
		entry.Desc = desc;
		if (desc.B == desc.A)
		{
			WI_PHYSICS_LOG(Warning, "Joint of {0}: it is connected to its own body, so it is not made", desc.A.to_string());
			entry.Failed = true;
			return;
		}
		try_make(entry);
	}

	void JointSet::destroy(BodyKey owner)
	{
		if (const auto it = m_Joints.find(owner); it != m_Joints.end())
		{
			unmake(it->second);
			m_Joints.erase(it);
		}
	}

	void JointSet::set_motor(BodyKey owner, JointMotorMode mode, float target, float maxForce)
	{
		const auto it = m_Joints.find(owner);
		if (it == m_Joints.end())
		{
			return;
		}
		Entry& entry = it->second;
		entry.Desc.MotorMode = mode;
		entry.Desc.MotorTarget = std::isfinite(target) ? target : 0.0f;
		entry.Desc.MotorMaxForce = std::isfinite(maxForce) ? maxForce : 0.0f;
		if (entry.Constraint != nullptr)
		{
			apply_joint_motor(*entry.Constraint, entry.Desc.Type, mode, entry.Desc.MotorTarget, entry.Desc.MotorMaxForce);
			wake_bodies(*entry.Constraint);
		}
	}

	void JointSet::set_limits(BodyKey owner, float min, float max)
	{
		const auto it = m_Joints.find(owner);
		if (it == m_Joints.end() || !std::isfinite(min) || !std::isfinite(max) || min > max)
		{
			return;
		}
		Entry& entry = it->second;
		entry.Desc.LimitMin = min;
		entry.Desc.LimitMax = max;
		if (entry.Constraint != nullptr)
		{
			apply_joint_limits(*entry.Constraint, entry.Desc.Type, min, max);
			wake_bodies(*entry.Constraint);
		}
	}

	void JointSet::body_created(BodyKey key)
	{
		for (auto& [owner, entry] : m_Joints)
		{
			if (entry.Constraint == nullptr && !entry.Failed && (entry.Desc.A == key || entry.Desc.B == key))
			{
				try_make(entry);
			}
		}
	}

	void JointSet::body_destroyed(BodyKey key)
	{
		for (auto& [owner, entry] : m_Joints)
		{
			if (entry.Constraint != nullptr && (entry.Desc.A == key || entry.Desc.B == key))
			{
				unmake(entry);
			}
		}
	}

	void JointSet::clear()
	{
		for (auto& [owner, entry] : m_Joints)
		{
			unmake(entry);
		}
		m_Joints.clear();
	}

	u32 JointSet::made_count() const
	{
		u32 count = 0;
		for (const auto& [owner, entry] : m_Joints)
		{
			count += entry.Constraint != nullptr ? 1u : 0u;
		}
		return count;
	}

	void JointSet::try_make(Entry& entry)
	{
		const JPH::BodyID idA = m_Lookup(entry.Desc.A);
		const bool toWorld = !entry.Desc.B.valid();
		const JPH::BodyID idB = toWorld ? JPH::BodyID() : m_Lookup(entry.Desc.B);
		if (idA.IsInvalid() || (!toWorld && idB.IsInvalid()))
		{
			return; // waits for the body
		}

		const JPH::BodyLockInterface& locks = m_System.GetBodyLockInterfaceNoLock();
		JPH::Body* bodyA = locks.TryGetBody(idA);
		JPH::Body* bodyB = toWorld ? &JPH::Body::sFixedToWorld : locks.TryGetBody(idB);
		if (bodyA == nullptr || bodyB == nullptr)
		{
			return;
		}

		std::string error;
		JPH::Ref<JPH::TwoBodyConstraint> constraint = build_joint(entry.Desc, *bodyA, *bodyB, error);
		if (constraint == nullptr)
		{
			WI_PHYSICS_LOG(Warning, "Joint of {0} is not made: {1}", entry.Desc.A.to_string(), error);
			entry.Failed = true;
			return;
		}
		m_System.AddConstraint(constraint);
		entry.Constraint = std::move(constraint);
		if (!entry.Desc.CollideConnected && !toWorld)
		{
			const JPH::uint32 first = idA.GetIndexAndSequenceNumber();
			const JPH::uint32 second = idB.GetIndexAndSequenceNumber();
			entry.NoCollide = { std::min(first, second), std::max(first, second) };
			m_NoCollide.insert(entry.NoCollide);
		}
	}

	void JointSet::wake_bodies(const JPH::TwoBodyConstraint& constraint)
	{
		JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
		for (const JPH::Body* body : { constraint.GetBody1(), constraint.GetBody2() })
		{
			if (body != nullptr && !body->IsStatic() && !body->IsActive())
			{
				bodies.ActivateBody(body->GetID());
			}
		}
	}

	void JointSet::unmake(Entry& entry)
	{
		if (entry.Constraint != nullptr)
		{
			wake_bodies(*entry.Constraint); // without the joint the bodies it held have to be free to move
			m_System.RemoveConstraint(entry.Constraint);
			entry.Constraint = nullptr;
		}
		m_NoCollide.erase(entry.NoCollide);
		entry.NoCollide = {};
	}

	void JointSet::check_breaks(float dt, std::vector<PhysicsEvent>& events)
	{
		if (!(dt > 0.0f))
		{
			return;
		}
		for (auto& [owner, entry] : m_Joints)
		{
			const bool breakable = entry.Desc.BreakForce > 0.0f || entry.Desc.BreakTorque > 0.0f;
			if (entry.Constraint == nullptr || !breakable)
			{
				continue;
			}
			const JointReaction reaction = joint_reaction(*entry.Constraint, entry.Desc.Type);
			const bool force = entry.Desc.BreakForce > 0.0f && reaction.Linear / dt > entry.Desc.BreakForce;
			const bool torque = entry.Desc.BreakTorque > 0.0f && reaction.Angular / dt > entry.Desc.BreakTorque;
			if (force || torque)
			{
				PhysicsEvent event;
				event.Kind = PhysicsEventKind::JointBreak;
				event.A = owner;
				event.B = entry.Desc.B;
				events.push_back(event);
				unmake(entry);
				entry.Failed = true;
			}
		}
	}
}
