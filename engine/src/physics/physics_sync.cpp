#include <pch.h>
#include "physics_sync.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wiwa
{
	namespace
	{
		struct Pose
		{
			glm::vec3 Position{ 0.0f };
			glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 Scale{ 1.0f };
		};

		// Position, rotation and scale of a world matrix (no shear). A zero-scale axis is rebuilt from the other two.
		Pose decompose(const glm::mat4& matrix)
		{
			Pose pose;
			pose.Position = glm::vec3(matrix[3]);
			glm::vec3 axes[3];
			bool valid[3];
			for (int i = 0; i < 3; i++)
			{
				axes[i] = glm::vec3(matrix[i]);
				pose.Scale[i] = glm::length(axes[i]);
				valid[i] = pose.Scale[i] > 1e-6f && std::isfinite(pose.Scale[i]);
				axes[i] = valid[i] ? axes[i] / pose.Scale[i] : glm::vec3(0.0f);
			}

			if (valid[0] && valid[1] && !valid[2]) { axes[2] = glm::normalize(glm::cross(axes[0], axes[1])); }
			else if (valid[0] && !valid[1] && valid[2]) { axes[1] = glm::normalize(glm::cross(axes[2], axes[0])); }
			else if (!valid[0] && valid[1] && valid[2]) { axes[0] = glm::normalize(glm::cross(axes[1], axes[2])); }
			else if (!(valid[0] && valid[1] && valid[2]))
			{
				return Pose{ pose.Position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), pose.Scale };
			}

			glm::mat3 rotation(axes[0], axes[1], axes[2]);
			if (glm::determinant(rotation) < 0.0f) // mirrored: the mirror goes into the scale
			{
				rotation[0] = -rotation[0];
				pose.Scale.x = -pose.Scale.x;
			}
			pose.Rotation = glm::normalize(glm::quat_cast(rotation));
			if (!std::isfinite(pose.Rotation.w))
			{
				pose.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			}
			return pose;
		}

		glm::quat euler_degrees(glm::vec3 degrees) { return glm::normalize(glm::quat(glm::radians(degrees))); }

		ShapeKind shape_kind(ColliderShape shape)
		{
			switch (shape)
			{
			case ColliderShape::Box: return ShapeKind::Box;
			case ColliderShape::Sphere: return ShapeKind::Sphere;
			case ColliderShape::Capsule: return ShapeKind::Capsule;
			case ColliderShape::Cylinder: return ShapeKind::Cylinder;
			default: return ShapeKind::Placeholder; // ConvexHull and Mesh come with cooked shapes
			}
		}

		bool close_to(float a, float b) { return std::abs(a - b) <= 1e-4f * std::max({ 1.0f, std::abs(a), std::abs(b) }); }
		bool close_to(glm::vec3 a, glm::vec3 b) { return close_to(a.x, b.x) && close_to(a.y, b.y) && close_to(a.z, b.z); }
		bool close_to(glm::quat a, glm::quat b) { return std::abs(glm::dot(a, b)) >= 1.0f - 1e-6f; }
		bool close_to(const glm::mat4& a, const glm::mat4& b)
		{
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					if (std::abs(a[i][j] - b[i][j]) > 1e-5f * std::max(1.0f, std::abs(a[i][j])))
					{
						return false;
					}
				}
			}
			return true;
		}

		// The same shape, give or take the float noise of taking world matrices apart (a turning body's scale is never exactly 1).
		bool same_shape(const ShapeDesc& a, const ShapeDesc& b)
		{
			if (a.Kind != b.Kind || a.Source != b.Source || !(a.Material == b.Material) || a.Children.size() != b.Children.size()
				|| !close_to(a.Size, b.Size) || !close_to(a.Offset, b.Offset) || !close_to(a.Rotation, b.Rotation) || !close_to(a.Scale, b.Scale))
			{
				return false;
			}
			for (size_t i = 0; i < a.Children.size(); i++)
			{
				if (!same_shape(a.Children[i], b.Children[i]))
				{
					return false;
				}
			}
			return true;
		}

		// Everything but the pose and the shape.
		bool same_body(const BodyDesc& a, const BodyDesc& b)
		{
			return a.Motion == b.Motion && a.MassOverride == b.MassOverride && a.LinearDamping == b.LinearDamping && a.AngularDamping == b.AngularDamping
				&& a.GravityFactor == b.GravityFactor && a.ContinuousCollision == b.ContinuousCollision && a.AllowSleeping == b.AllowSleeping
				&& a.ReportContacts == b.ReportContacts && std::equal(std::begin(a.LockPosition), std::end(a.LockPosition), std::begin(b.LockPosition))
				&& std::equal(std::begin(a.LockRotation), std::end(a.LockRotation), std::begin(b.LockRotation)) && a.Layer == b.Layer && a.IsTrigger == b.IsTrigger;
		}

		// The entity whose body a collider belongs to: itself when it has a RigidBody, else the nearest ancestor that has one, else itself
		// (a static body).
		entt::entity owner_of(const entt::registry& registry, entt::entity entity)
		{
			for (entt::entity e = entity; e != entt::null; e = registry.get<Hierarchy>(e).Parent)
			{
				if (registry.all_of<RigidBody>(e))
				{
					return e;
				}
			}
			return entity;
		}

		struct Collected
		{
			entt::entity Entity{ entt::null };
			BodyDesc Desc{};
		};

		// The bodies the scene asks for now, by key. Needs current WorldTransforms.
		std::unordered_map<BodyKey, Collected> collect(const Scene& scene)
		{
			const entt::registry& registry = scene.registry();

			std::unordered_map<entt::entity, std::vector<entt::entity>> byOwner;
			for (const entt::entity entity : registry.view<Collider>())
			{
				byOwner[owner_of(registry, entity)].push_back(entity);
			}

			std::unordered_map<BodyKey, Collected> bodies;
			bodies.reserve(byOwner.size());
			for (auto& [owner, colliders] : byOwner)
			{
				// the owner's own collider first, then the others in a fixed order, so the same scene gives the same shape every frame
				std::sort(colliders.begin(), colliders.end(), [&](entt::entity a, entt::entity b)
				{
					if ((a == owner) != (b == owner))
					{
						return a == owner;
					}
					return registry.get<Id>(a).Guid < registry.get<Id>(b).Guid;
				});

				const Pose body = decompose(registry.get<WorldTransform>(owner).Matrix);
				const glm::quat toBody = glm::inverse(body.Rotation);

				Collected collected;
				collected.Entity = owner;
				BodyDesc& desc = collected.Desc;
				if (const RigidBody* rigid = registry.try_get<RigidBody>(owner))
				{
					desc.Motion = rigid->Motion;
					desc.MassOverride = std::max(rigid->MassOverride, 0.0f); // negative: from the shape
					desc.LinearDamping = rigid->LinearDamping;
					desc.AngularDamping = rigid->AngularDamping;
					desc.GravityFactor = rigid->GravityFactor;
					desc.ContinuousCollision = rigid->ContinuousCollision;
					desc.AllowSleeping = rigid->AllowSleeping;
					desc.ReportContacts = rigid->ReportContacts;
					desc.LockPosition[0] = rigid->LockPositionX;
					desc.LockPosition[1] = rigid->LockPositionY;
					desc.LockPosition[2] = rigid->LockPositionZ;
					desc.LockRotation[0] = rigid->LockRotationX;
					desc.LockRotation[1] = rigid->LockRotationY;
					desc.LockRotation[2] = rigid->LockRotationZ;
				}
				else
				{
					desc.Motion = MotionType::Static;
				}
				desc.Position = body.Position;
				desc.Rotation = body.Rotation;

				// one sensor flag and layer per body: the first collider's (the owner's own when it has one)
				const Collider& first = registry.get<Collider>(colliders.front());
				desc.IsTrigger = first.IsTrigger;
				desc.Layer = first.Layer;

				std::vector<ShapeDesc> parts;
				parts.reserve(colliders.size());
				for (const entt::entity entity : colliders)
				{
					const Collider& collider = registry.get<Collider>(entity);
					ShapeDesc part;
					part.Kind = shape_kind(collider.Shape);
					part.Size = collider.Size;
					part.Source = registry.get<Id>(entity).Guid;
					if (entity == owner)
					{
						part.Scale = body.Scale;
						part.Offset = collider.Offset * body.Scale;
						part.Rotation = euler_degrees(collider.RotationEuler);
					}
					else
					{
						// the collider's entity placed in the body's frame
						const Pose pose = decompose(registry.get<WorldTransform>(entity).Matrix);
						const glm::vec3 position = toBody * (pose.Position - body.Position);
						const glm::quat rotation = glm::normalize(toBody * pose.Rotation);
						part.Scale = pose.Scale;
						part.Offset = position + rotation * (collider.Offset * pose.Scale);
						part.Rotation = glm::normalize(rotation * euler_degrees(collider.RotationEuler));
					}
					parts.push_back(std::move(part));
				}

				if (parts.size() == 1)
				{
					desc.Shape = std::move(parts.front());
				}
				else
				{
					desc.Shape = ShapeDesc{};
					desc.Shape.Kind = ShapeKind::Compound;
					desc.Shape.Source = registry.get<Id>(owner).Guid;
					desc.Shape.Children = std::move(parts);
				}
				bodies.emplace(registry.get<Id>(owner).Guid, std::move(collected));
			}
			return bodies;
		}
	}

	void PhysicsSync::build(Scene& scene, PhysicsThread& thread)
	{
		clear();
		m_Generation = thread.generation();
		push_changes(scene, thread);
	}

	void PhysicsSync::push_changes(Scene& scene, PhysicsThread& thread)
	{
		const entt::registry& registry = scene.registry();
		std::unordered_map<BodyKey, Collected> wanted = collect(scene);

		// gone: the entity was destroyed, or lost its collider, or its colliders now belong to another body
		for (auto it = m_Bodies.begin(); it != m_Bodies.end();)
		{
			if (!wanted.contains(it->first))
			{
				thread.push(DestroyBody{ it->first }, m_Generation);
				it = m_Bodies.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (auto& [key, collected] : wanted)
		{
			const glm::mat4& world = registry.get<WorldTransform>(collected.Entity).Matrix;
			const auto it = m_Bodies.find(key);
			if (it == m_Bodies.end() || !same_body(it->second.Desc, collected.Desc))
			{
				// new, or its RigidBody changed: (re)made where its entity is now
				thread.push(CreateBody{ key, collected.Desc }, m_Generation);
				m_Bodies[key] = Tracked{ std::move(collected.Desc), world };
				continue;
			}

			Tracked& tracked = it->second;
			if (!same_shape(tracked.Desc.Shape, collected.Desc.Shape))
			{
				thread.push(ReplaceShape{ key, collected.Desc.Shape }, m_Generation);
				tracked.Desc.Shape = std::move(collected.Desc.Shape);
			}

			// static and kinematic bodies go where the scene puts them; dynamic ones move by themselves
			if (collected.Desc.Motion != MotionType::Dynamic && !close_to(tracked.LastWorld, world))
			{
				if (collected.Desc.Motion == MotionType::Kinematic)
				{
					thread.push(SetKinematicTarget{ key, collected.Desc.Position, collected.Desc.Rotation }, m_Generation);
				}
				else
				{
					thread.push(SetTransform{ key, collected.Desc.Position, collected.Desc.Rotation }, m_Generation);
				}
				tracked.LastWorld = world;
			}
		}
	}

	void PhysicsSync::apply_poses(Scene& scene, const PhysicsStepSnapshot& prev, const PhysicsStepSnapshot& latest, float alpha)
	{
		if (latest.World != m_Generation || m_Generation == 0)
		{
			return;
		}
		const bool interpolate = &prev != &latest && prev.World == latest.World && alpha < 1.0f;
		alpha = std::clamp(alpha, 0.0f, 1.0f);

		std::unordered_map<BodyKey, const BodyPose*> previous;
		if (interpolate)
		{
			previous.reserve(prev.Poses.size());
			for (const BodyPose& pose : prev.Poses)
			{
				previous.emplace(pose.Key, &pose);
			}
		}

		entt::registry& registry = scene.registry();
		for (const BodyPose& pose : latest.Poses)
		{
			const auto tracked = m_Bodies.find(pose.Key);
			if (tracked == m_Bodies.end() || tracked->second.Desc.Motion != MotionType::Dynamic)
			{
				continue;
			}
			const entt::entity entity = scene.find(pose.Key);
			if (entity == entt::null || !registry.valid(entity))
			{
				continue; // destroyed since: its DestroyBody is on the way
			}

			glm::vec3 position = pose.Position;
			glm::quat rotation = pose.Rotation;
			if (interpolate)
			{
				if (const auto before = previous.find(pose.Key); before != previous.end())
				{
					position = glm::mix(before->second->Position, pose.Position, alpha);
					rotation = glm::slerp(before->second->Rotation, pose.Rotation, alpha);
				}
			}

			// world to local, through the parent; the local scale stays
			Transform& transform = registry.get<Transform>(entity);
			const entt::entity parent = registry.get<Hierarchy>(entity).Parent;
			if (parent == entt::null)
			{
				transform.Position = position;
				transform.Rotation = glm::normalize(rotation);
			}
			else
			{
				const glm::mat4& parentWorld = registry.get<WorldTransform>(parent).Matrix;
				transform.Position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(position, 1.0f));
				transform.Rotation = glm::normalize(glm::inverse(decompose(parentWorld).Rotation) * rotation);
			}
		}
	}

	void PhysicsSync::clear()
	{
		m_Bodies.clear();
	}
}
