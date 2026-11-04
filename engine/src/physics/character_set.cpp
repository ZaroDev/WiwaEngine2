#include <pch.h>
#include "character_set.h"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

#include "physics_layers_jolt.h"

namespace wiwa
{
	namespace
	{
		constexpr float kMinRadius = 0.05f;

		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		JPH::Vec3 to_jolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		float sane(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

		GroundState ground_state_of(const JPH::CharacterVirtual& character)
		{
			switch (character.GetGroundState())
			{
			case JPH::CharacterBase::EGroundState::OnGround: return GroundState::Grounded;
			case JPH::CharacterBase::EGroundState::OnSteepGround: return GroundState::Sliding;
			default: return GroundState::Airborne;
			}
		}
	}

	void CharacterSet::create(BodyKey key, const CharacterDesc& wanted)
	{
		if (!key.valid())
		{
			return;
		}
		destroy(key); // a CreateCharacter for a key that has a character replaces it

		Entry entry;
		entry.Desc = wanted;
		CharacterDesc& desc = entry.Desc;
		const bool sizesOk = std::isfinite(desc.Radius) && std::isfinite(desc.Height) && desc.Radius >= kMinRadius && desc.Height >= 2.0f * desc.Radius + 0.01f;
		if (!sizesOk)
		{
			WI_PHYSICS_LOG(Warning, "Character {0}: Radius {1} and Height {2} are not a capsule; they are made one", key.to_string(), desc.Radius, desc.Height);
		}
		desc.Radius = std::max(sane(desc.Radius, 0.4f), kMinRadius);
		desc.Height = std::max(sane(desc.Height, 1.8f), 2.0f * desc.Radius + 0.01f);
		desc.StepHeight = std::max(sane(desc.StepHeight, 0.35f), 0.0f);
		desc.MaxSlopeAngle = std::clamp(sane(desc.MaxSlopeAngle, 45.0f), 0.0f, 89.0f);
		desc.Mass = std::max(sane(desc.Mass, 70.0f), 1.0f);
		desc.PushStrength = std::max(sane(desc.PushStrength, 100.0f), 0.0f);
		desc.GravityFactor = sane(desc.GravityFactor, 1.0f);
		desc.Offset = finite(desc.Offset) ? desc.Offset : glm::vec3(0.0f);
		desc.Position = finite(desc.Position) ? desc.Position : glm::vec3(0.0f);

		entry.Shape = new JPH::CapsuleShape(desc.Height * 0.5f - desc.Radius, desc.Radius);

		JPH::CharacterVirtualSettings settings;
		settings.mUp = JPH::Vec3::sAxisY();
		settings.mShape = entry.Shape;
		settings.mMass = desc.Mass;
		settings.mMaxStrength = desc.PushStrength;
		settings.mMaxSlopeAngle = glm::radians(desc.MaxSlopeAngle);
		settings.mShapeOffset = to_jolt(desc.Offset);
		// a contact supports the character when it is on the lower cap: below the top of that cap, measured from the character position
		settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -(desc.Offset.y - desc.Height * 0.5f + desc.Radius));
		settings.mInnerBodyShape = entry.Shape;
		settings.mInnerBodyLayer = to_object_layer(desc.Layer, BodyCategory::Moving);
		entry.Character = new JPH::CharacterVirtual(&settings, JPH::RVec3(desc.Position.x, desc.Position.y, desc.Position.z), JPH::Quat::sIdentity(), &m_System);

		if (m_OnBody)
		{
			m_OnBody(entry.Character->GetInnerBodyID(), key, true);
		}
		m_Characters[key] = std::move(entry);
	}

	void CharacterSet::destroy(BodyKey key)
	{
		const auto it = m_Characters.find(key);
		if (it == m_Characters.end())
		{
			return;
		}
		if (m_OnBody)
		{
			m_OnBody(it->second.Character->GetInnerBodyID(), key, false);
		}
		m_Characters.erase(it);
	}

	void CharacterSet::set_velocity(BodyKey key, glm::vec3 velocity)
	{
		if (const auto it = m_Characters.find(key); it != m_Characters.end() && finite(velocity))
		{
			it->second.Desired = glm::vec3(velocity.x, 0.0f, velocity.z);
		}
	}

	void CharacterSet::jump(BodyKey key, float speed)
	{
		if (const auto it = m_Characters.find(key); it != m_Characters.end() && std::isfinite(speed) && speed > 0.0f)
		{
			it->second.PendingJump = speed;
		}
	}

	void CharacterSet::teleport(BodyKey key, glm::vec3 position)
	{
		if (const auto it = m_Characters.find(key); it != m_Characters.end() && finite(position))
		{
			it->second.Character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
			it->second.Character->SetLinearVelocity(JPH::Vec3::sZero());
			it->second.Moved = glm::vec3(0.0f);
		}
	}

	void CharacterSet::step(float dt, JPH::TempAllocator& allocator)
	{
		for (auto& [key, entry] : m_Characters)
		{
			JPH::CharacterVirtual& character = *entry.Character;
			const JPH::Vec3 gravity = m_System.GetGravity() * entry.Desc.GravityFactor;

			// like the Jolt character example: on ground that is not falling away take its velocity (and the jump), else keep falling
			const JPH::Vec3 vertical(0.0f, character.GetLinearVelocity().GetY(), 0.0f);
			const JPH::Vec3 ground = character.GetGroundVelocity();
			const bool towardsGround = (vertical.GetY() - ground.GetY()) < 0.1f;
			JPH::Vec3 velocity = vertical;
			if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && towardsGround)
			{
				velocity = ground;
				if (entry.PendingJump > 0.0f)
				{
					velocity += JPH::Vec3(0.0f, entry.PendingJump, 0.0f);
				}
			}
			entry.PendingJump = 0.0f;
			velocity += gravity * dt;
			velocity += to_jolt(entry.Desired);
			character.SetLinearVelocity(velocity);
			const JPH::RVec3 before = character.GetPosition();

			JPH::CharacterVirtual::ExtendedUpdateSettings update;
			update.mStickToFloorStepDown = JPH::Vec3(0.0f, -entry.Desc.StepHeight, 0.0f);
			update.mWalkStairsStepUp = JPH::Vec3(0.0f, entry.Desc.StepHeight, 0.0f);
			const JPH::ObjectLayer layer = to_object_layer(entry.Desc.Layer, BodyCategory::Moving);
			character.ExtendedUpdate(dt, gravity, update, m_System.GetDefaultBroadPhaseLayerFilter(layer), m_System.GetDefaultLayerFilter(layer),
				JPH::IgnoreSingleBodyFilter(character.GetInnerBodyID()), JPH::ShapeFilter(), allocator);
			entry.Moved = to_glm(JPH::Vec3(character.GetPosition() - before)) / dt;
		}
	}

	void CharacterSet::write(PhysicsStepSnapshot& snapshot) const
	{
		snapshot.CharacterCount = count();
		snapshot.Characters.reserve(m_Characters.size());
		for (const auto& [key, entry] : m_Characters)
		{
			const JPH::CharacterVirtual& character = *entry.Character;
			CharacterPose pose;
			pose.Key = key;
			const JPH::RVec3 position = character.GetPosition();
			pose.Position = glm::vec3(position.GetX(), position.GetY(), position.GetZ());
			pose.Velocity = entry.Moved;
			pose.Ground = ground_state_of(character);
			if (character.IsSupported())
			{
				pose.GroundBody = m_KeyOf ? m_KeyOf(character.GetGroundBodyID()) : BodyKey{};
				pose.GroundNormal = to_glm(character.GetGroundNormal());
			}
			snapshot.Characters.push_back(pose);
		}
	}

	void CharacterSet::clear()
	{
		for (auto& [key, entry] : m_Characters)
		{
			if (m_OnBody)
			{
				m_OnBody(entry.Character->GetInnerBodyID(), key, false);
			}
		}
		m_Characters.clear();
	}

	BodyKey CharacterSet::key_of_inner(JPH::BodyID id) const
	{
		for (const auto& [key, entry] : m_Characters)
		{
			if (entry.Character->GetInnerBodyID() == id)
			{
				return key;
			}
		}
		return BodyKey{};
	}

	std::vector<CharacterSet::View> CharacterSet::views() const
	{
		std::vector<View> result;
		result.reserve(m_Characters.size());
		for (const auto& [key, entry] : m_Characters)
		{
			View view;
			view.Key = key;
			const JPH::RVec3 center = entry.Character->GetCenterOfMassPosition();
			view.Position = glm::vec3(center.GetX(), center.GetY(), center.GetZ());
			view.Layer = entry.Desc.Layer;
			view.Shape = entry.Shape;
			result.push_back(view);
		}
		return result;
	}
}
