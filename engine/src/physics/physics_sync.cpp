#include <pch.h>
#include "physics_sync.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <assets/asset_manager.h>
#include <core/engine.h>

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
			case ColliderShape::ConvexHull: return ShapeKind::ConvexHull;
			case ColliderShape::Mesh: return ShapeKind::Mesh;
			default: return ShapeKind::Placeholder;
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
			if (a.Kind != b.Kind || a.Source != b.Source || !(a.Material == b.Material) || a.Cooked != b.Cooked || a.Children.size() != b.Children.size()
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

		// Everything but the pose, the shape and the layer (a layer change is a SetLayer).
		bool same_body(const BodyDesc& a, const BodyDesc& b)
		{
			return a.Motion == b.Motion && a.MassOverride == b.MassOverride && a.CenterOfMassOffset == b.CenterOfMassOffset && a.LinearDamping == b.LinearDamping && a.AngularDamping == b.AngularDamping
				&& a.GravityFactor == b.GravityFactor && a.ContinuousCollision == b.ContinuousCollision && a.AllowSleeping == b.AllowSleeping
				&& a.ReportContacts == b.ReportContacts && std::equal(std::begin(a.LockPosition), std::end(a.LockPosition), std::begin(b.LockPosition))
				&& std::equal(std::begin(a.LockRotation), std::end(a.LockRotation), std::begin(b.LockRotation)) && a.IsTrigger == b.IsTrigger;
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
		using ShapeResolver = std::function<std::shared_ptr<const CookedShapeData>(AssetRef mesh, CookedShapeKind kind, EntityGuid collider)>;

		std::unordered_map<BodyKey, Collected> collect(const Scene& scene, const std::function<PhysicsMaterialData(AssetRef)>& resolve_material, const ShapeResolver& resolve_shape,
			const std::function<void(entt::entity)>& wheel_collider)
		{
			const entt::registry& registry = scene.registry();

			std::unordered_map<entt::entity, std::vector<entt::entity>> byOwner;
			for (const entt::entity entity : registry.view<Collider>())
			{
				if (registry.all_of<VehicleWheel>(entity))
				{
					wheel_collider(entity); // a wheel's collider would hold the car up off its suspension: left out
					continue;
				}
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
					desc.CenterOfMassOffset = rigid->CenterOfMassOffset;
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
					part.Material = resolve_material(collider.Material);
					if (part.Kind == ShapeKind::ConvexHull || part.Kind == ShapeKind::Mesh)
					{
						part.Cooked = resolve_shape(collider.Mesh, part.Kind == ShapeKind::Mesh ? CookedShapeKind::Mesh : CookedShapeKind::ConvexHull, part.Source);
					}
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
		std::unordered_map<BodyKey, Collected> wanted = collect(scene, [this](AssetRef ref) { return resolve_material(ref); },
			[this](AssetRef mesh, CookedShapeKind kind, EntityGuid collider) { return resolve_shape(mesh, kind, collider); },
			[&](entt::entity entity)
			{
				const EntityGuid guid = registry.get<Id>(entity).Guid;
				if (m_WarnedColliders.insert(guid).second)
				{
					WI_PHYSICS_LOG(Warning, "Collider {0} is on a VehicleWheel: it is left out of the body (the wheel is the suspension's)", guid.to_string());
				}
			});

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
			if (tracked.Desc.Layer != collected.Desc.Layer)
			{
				thread.push(SetLayer{ key, collected.Desc.Layer }, m_Generation);
				tracked.Desc.Layer = collected.Desc.Layer;
			}
			if (!same_shape(tracked.Desc.Shape, collected.Desc.Shape))
			{
				// only the material of a single shape changed (its asset finished loading): the cheap command says so
				ShapeDesc sameMaterial = collected.Desc.Shape;
				sameMaterial.Material = tracked.Desc.Shape.Material;
				if (collected.Desc.Shape.Kind != ShapeKind::Compound && same_shape(tracked.Desc.Shape, sameMaterial))
				{
					thread.push(SetMaterial{ key, collected.Desc.Shape.Material }, m_Generation);
				}
				else
				{
					thread.push(ReplaceShape{ key, collected.Desc.Shape }, m_Generation);
				}
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

		push_joints(scene, thread);
		push_characters(scene, thread);
		push_vehicles(scene, thread);
	}

	void PhysicsSync::push_vehicles(const Scene& scene, PhysicsThread& thread)
	{
		const entt::registry& registry = scene.registry();
		const auto warn = [&](EntityGuid entity, const std::string& text)
		{
			if (m_WarnedVehicles.insert(entity).second)
			{
				WI_PHYSICS_LOG(Warning, "Entity {0}: {1}", entity.to_string(), text);
			}
		};

		std::unordered_map<BodyKey, std::pair<VehicleDesc, std::unordered_map<EntityGuid, WheelAuthored>>> wanted;
		for (const entt::entity entity : registry.view<Vehicle>())
		{
			const Vehicle& vehicle = registry.get<Vehicle>(entity);
			const BodyKey key = registry.get<Id>(entity).Guid;
			if (!vehicle.Enabled)
			{
				continue;
			}
			const auto body = m_Bodies.find(key);
			if (body == m_Bodies.end() || body->second.Desc.Motion != MotionType::Dynamic)
			{
				warn(key, "a Vehicle needs a dynamic RigidBody and a Collider on its entity; it is not made");
				continue;
			}
			const std::optional<VehicleSettingsData> settings = resolve_vehicle(vehicle.Settings);
			if (!settings)
			{
				continue; // loading, or failed (warned)
			}

			// the wheels: VehicleWheels below the body, not below another body
			std::vector<entt::entity> wheels;
			std::vector<entt::entity> open;
			for (entt::entity child = registry.get<Hierarchy>(entity).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
			{
				open.push_back(child);
			}
			while (!open.empty())
			{
				const entt::entity e = open.back();
				open.pop_back();
				if (registry.all_of<RigidBody>(e))
				{
					continue;
				}
				if (registry.all_of<VehicleWheel>(e))
				{
					wheels.push_back(e);
				}
				for (entt::entity child = registry.get<Hierarchy>(e).FirstChild; child != entt::null; child = registry.get<Hierarchy>(child).NextSibling)
				{
					open.push_back(child);
				}
			}
			std::sort(wheels.begin(), wheels.end(), [&](entt::entity a, entt::entity b) { return registry.get<Id>(a).Guid < registry.get<Id>(b).Guid; });

			const glm::mat4& bodyWorld = registry.get<WorldTransform>(entity).Matrix;
			const Pose bodyPose = decompose(bodyWorld);
			const glm::mat4 toBody = glm::inverse(glm::translate(glm::mat4(1.0f), bodyPose.Position) * glm::mat4_cast(bodyPose.Rotation));
			const auto tracked = m_Vehicles.find(key);
			VehicleDesc desc;
			desc.Settings = *settings;
			std::unordered_map<EntityGuid, WheelAuthored> authored;
			for (const entt::entity wheelEntity : wheels)
			{
				const EntityGuid guid = registry.get<Id>(wheelEntity).Guid;
				WheelAuthored place;
				bool known = false;
				if (tracked != m_Vehicles.end())
				{
					if (const auto cached = tracked->second.Wheels.find(guid); cached != tracked->second.Wheels.end())
					{
						place = cached->second;
						known = true;
					}
				}
				if (!known)
				{
					const Pose wheelPose = decompose(toBody * registry.get<WorldTransform>(wheelEntity).Matrix);
					place = WheelAuthored{ wheelPose.Position, wheelPose.Rotation };
				}
				authored.emplace(guid, place);

				const VehicleWheel& component = registry.get<VehicleWheel>(wheelEntity);
				WheelDesc wheel;
				wheel.Entity = guid;
				wheel.Position = place.Position;
				wheel.Radius = component.Radius;
				wheel.Width = component.Width;
				wheel.SuspensionMinLength = component.SuspensionMinLength;
				wheel.SuspensionMaxLength = component.SuspensionMaxLength;
				wheel.SuspensionFrequency = component.SuspensionFrequency;
				wheel.SuspensionDamping = component.SuspensionDamping;
				wheel.Steers = component.Steers;
				wheel.Brakes = component.Brakes;
				wheel.Handbrake = component.Handbrake;
				wheel.MaxSteerAngle = component.MaxSteerAngle;
				wheel.Axle = component.Axle;
				wheel.Side = component.Side;
				wheel.Inertia = component.Inertia;
				desc.Wheels.push_back(wheel);
			}
			if (desc.Wheels.empty())
			{
				warn(key, "a Vehicle has no VehicleWheel below it; it is not made");
				continue;
			}
			wanted.emplace(key, std::make_pair(std::move(desc), std::move(authored)));
		}

		// VehicleWheels with no Vehicle above them
		for (const entt::entity entity : registry.view<VehicleWheel>())
		{
			bool found = false;
			for (entt::entity e = registry.get<Hierarchy>(entity).Parent; e != entt::null && !found; e = registry.get<Hierarchy>(e).Parent)
			{
				found = registry.all_of<Vehicle>(e);
			}
			if (!found)
			{
				warn(registry.get<Id>(entity).Guid, "a VehicleWheel has no Vehicle above it; it does nothing");
			}
		}

		for (auto it = m_Vehicles.begin(); it != m_Vehicles.end();)
		{
			if (!wanted.contains(it->first))
			{
				thread.push(DestroyVehicle{ it->first }, m_Generation);
				it = m_Vehicles.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (auto& [key, entry] : wanted)
		{
			TrackedVehicle& tracked = m_Vehicles[key];
			tracked.Wheels = std::move(entry.second);
			if (!(tracked.Desc == entry.first))
			{
				thread.push(CreateVehicle{ key, entry.first }, m_Generation);
				tracked.Desc = std::move(entry.first);
				m_VehiclesSent++;
			}
		}
	}

	std::optional<VehicleSettingsData> PhysicsSync::resolve_vehicle(AssetRef ref)
	{
		if (!ref.valid())
		{
			return default_vehicle_settings();
		}
		const AssetGuid key = ref.resolved();
		auto it = m_VehicleSettings.find(key);
		if (it == m_VehicleSettings.end())
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (assets == nullptr)
			{
				return std::nullopt;
			}
			it = m_VehicleSettings.emplace(key, VehicleSettingsEntry{ assets->load<VehicleSettingsData>(ref) }).first;
		}
		VehicleSettingsEntry& entry = it->second;
		switch (entry.Settings.state())
		{
		case LoadState::Ready:
			if (const VehicleSettingsData* data = entry.Settings.get(); data != nullptr && vehicle_settings_valid(*data))
			{
				return *data;
			}
			[[fallthrough]];
		case LoadState::Failed:
			if (!entry.Warned)
			{
				entry.Warned = true;
				WI_PHYSICS_LOG(Warning, "Vehicle settings {0} are missing or could not be loaded: the vehicle is not made", ref.Guid.to_string());
			}
			return std::nullopt;
		default:
			return std::nullopt; // still loading
		}
	}

	void PhysicsSync::push_joints(const Scene& scene, PhysicsThread& thread)
	{
		const entt::registry& registry = scene.registry();
		std::unordered_map<BodyKey, JointDesc> wanted;
		for (const entt::entity entity : registry.view<Joint>())
		{
			const BodyKey owner = registry.get<Id>(entity).Guid;
			if (!m_Bodies.contains(owner))
			{
				continue; // no body on this entity (yet): nothing to hold the joint
			}
			const Joint& joint = registry.get<Joint>(entity);
			if (joint.Connected == owner)
			{
				if (m_WarnedJoints.insert(owner).second)
				{
					WI_PHYSICS_LOG(Warning, "Entity {0}: its Joint is connected to the entity itself; it is not made", owner.to_string());
				}
				continue;
			}
			JointDesc desc;
			desc.Type = joint.Type;
			desc.A = owner;
			desc.B = joint.Connected;
			desc.CollideConnected = joint.CollideConnected;
			desc.AnchorA = joint.AnchorA;
			desc.AnchorB = joint.AnchorB;
			desc.Axis = joint.Axis;
			desc.NormalAxis = joint.NormalAxis;
			desc.LimitMin = joint.LimitMin;
			desc.LimitMax = joint.LimitMax;
			desc.SwingHalfAngle = joint.SwingHalfAngle;
			desc.TwistHalfAngle = joint.TwistHalfAngle;
			desc.SpringFrequency = joint.SpringFrequency;
			desc.SpringDamping = joint.SpringDamping;
			desc.MotorMode = joint.MotorMode;
			desc.MotorTarget = joint.MotorTarget;
			desc.MotorMaxForce = joint.MotorMaxForce;
			desc.BreakForce = joint.BreakForce;
			desc.BreakTorque = joint.BreakTorque;
			wanted.emplace(owner, desc);
		}

		for (auto it = m_Joints.begin(); it != m_Joints.end();)
		{
			if (!wanted.contains(it->first))
			{
				thread.push(DestroyJoint{ it->first }, m_Generation);
				it = m_Joints.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (const auto& [owner, desc] : wanted)
		{
			const auto it = m_Joints.find(owner);
			if (it == m_Joints.end())
			{
				thread.push(CreateJoint{ desc }, m_Generation);
				m_Joints.emplace(owner, desc);
				continue;
			}
			JointDesc& sent = it->second;
			if (sent == desc)
			{
				continue;
			}
			// only the motor or the limits changed: the joint keeps its rest pose
			JointDesc rest = desc;
			rest.MotorMode = sent.MotorMode;
			rest.MotorTarget = sent.MotorTarget;
			rest.MotorMaxForce = sent.MotorMaxForce;
			rest.LimitMin = sent.LimitMin;
			rest.LimitMax = sent.LimitMax;
			if (!(rest == sent))
			{
				thread.push(CreateJoint{ desc }, m_Generation);
				sent = desc;
				continue;
			}
			if (desc.MotorMode != sent.MotorMode || desc.MotorTarget != sent.MotorTarget || desc.MotorMaxForce != sent.MotorMaxForce)
			{
				thread.push(SetJointMotor{ owner, desc.MotorMode, desc.MotorTarget, desc.MotorMaxForce }, m_Generation);
			}
			if (desc.LimitMin != sent.LimitMin || desc.LimitMax != sent.LimitMax)
			{
				thread.push(SetJointLimits{ owner, desc.LimitMin, desc.LimitMax }, m_Generation);
			}
			sent = desc;
		}
	}

	void PhysicsSync::push_characters(const Scene& scene, PhysicsThread& thread)
	{
		const entt::registry& registry = scene.registry();
		std::unordered_map<BodyKey, CharacterDesc> wanted;
		for (const entt::entity entity : registry.view<CharacterController>())
		{
			const BodyKey owner = registry.get<Id>(entity).Guid;
			if (registry.all_of<RigidBody>(entity) || registry.all_of<Collider>(entity))
			{
				if (m_WarnedCharacters.insert(owner).second)
				{
					WI_PHYSICS_LOG(Warning, "Entity {0}: a CharacterController cannot share its entity with a RigidBody or a Collider; it is not made", owner.to_string());
				}
				continue;
			}
			const CharacterController& controller = registry.get<CharacterController>(entity);
			CharacterDesc desc;
			desc.Radius = controller.Radius;
			desc.Height = controller.Height;
			desc.Offset = controller.Offset;
			desc.StepHeight = controller.StepHeight;
			desc.MaxSlopeAngle = controller.MaxSlopeAngle;
			desc.Mass = controller.Mass;
			desc.PushStrength = controller.PushStrength;
			desc.GravityFactor = controller.GravityFactor;
			desc.Layer = controller.Layer;
			desc.Position = glm::vec3(registry.get<WorldTransform>(entity).Matrix[3]);
			wanted.emplace(owner, desc);
		}

		for (auto it = m_Characters.begin(); it != m_Characters.end();)
		{
			if (!wanted.contains(it->first))
			{
				thread.push(DestroyCharacter{ it->first }, m_Generation);
				it = m_Characters.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (const auto& [owner, desc] : wanted)
		{
			const auto it = m_Characters.find(owner);
			if (it == m_Characters.end())
			{
				thread.push(CreateCharacter{ owner, desc }, m_Generation);
				m_Characters.emplace(owner, TrackedCharacter{ desc, desc.Position });
				continue;
			}
			TrackedCharacter& tracked = it->second;
			CharacterDesc sentWithoutPosition = tracked.Desc;
			sentWithoutPosition.Position = desc.Position;
			const bool sameFields = sentWithoutPosition.Radius == desc.Radius && sentWithoutPosition.Height == desc.Height && sentWithoutPosition.Offset == desc.Offset
				&& sentWithoutPosition.StepHeight == desc.StepHeight && sentWithoutPosition.MaxSlopeAngle == desc.MaxSlopeAngle && sentWithoutPosition.Mass == desc.Mass
				&& sentWithoutPosition.PushStrength == desc.PushStrength && sentWithoutPosition.GravityFactor == desc.GravityFactor && sentWithoutPosition.Layer == desc.Layer;
			if (!sameFields)
			{
				thread.push(CreateCharacter{ owner, desc }, m_Generation); // made again where its entity is
				tracked = TrackedCharacter{ desc, desc.Position };
			}
			else if (glm::length(desc.Position - tracked.Expected) > 1e-3f)
			{
				thread.push(TeleportCharacter{ owner, desc.Position }, m_Generation); // something other than the physics moved the entity
				tracked.Expected = desc.Position;
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
		std::unordered_map<BodyKey, std::pair<glm::vec3, glm::quat>> bodyNow; // the vehicles' bodies as just written, for their wheels
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

			if (m_Vehicles.contains(pose.Key))
			{
				bodyNow.emplace(pose.Key, std::make_pair(position, glm::normalize(rotation)));
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

		apply_wheels(scene, prev, latest, alpha, interpolate, bodyNow);

		// characters: only their position is written; the entity keeps its rotation and scale
		std::unordered_map<BodyKey, const CharacterPose*> previousCharacters;
		if (interpolate)
		{
			for (const CharacterPose& pose : prev.Characters)
			{
				previousCharacters.emplace(pose.Key, &pose);
			}
		}
		for (const CharacterPose& pose : latest.Characters)
		{
			const auto tracked = m_Characters.find(pose.Key);
			if (tracked == m_Characters.end())
			{
				continue;
			}
			const entt::entity entity = scene.find(pose.Key);
			if (entity == entt::null || !registry.valid(entity))
			{
				continue; // destroyed since: its DestroyCharacter is on the way
			}
			glm::vec3 position = pose.Position;
			if (interpolate)
			{
				if (const auto before = previousCharacters.find(pose.Key); before != previousCharacters.end())
				{
					position = glm::mix(before->second->Position, pose.Position, alpha);
				}
			}
			Transform& transform = registry.get<Transform>(entity);
			const entt::entity parent = registry.get<Hierarchy>(entity).Parent;
			transform.Position = parent == entt::null ? position : glm::vec3(glm::inverse(registry.get<WorldTransform>(parent).Matrix) * glm::vec4(position, 1.0f));
			tracked->second.Expected = position;
		}
	}

	void PhysicsSync::apply_wheels(Scene& scene, const PhysicsStepSnapshot& prev, const PhysicsStepSnapshot& latest, float alpha, bool interpolate,
		const std::unordered_map<BodyKey, std::pair<glm::vec3, glm::quat>>& bodyNow)
	{
		entt::registry& registry = scene.registry();
		for (const VehiclePose& vehicle : latest.Vehicles)
		{
			const auto tracked = m_Vehicles.find(vehicle.Body);
			const auto now = bodyNow.find(vehicle.Body);
			const entt::entity bodyEntity = scene.find(vehicle.Body);
			if (tracked == m_Vehicles.end() || now == bodyNow.end() || bodyEntity == entt::null || !registry.valid(bodyEntity))
			{
				continue;
			}
			const VehiclePose* before = nullptr;
			if (interpolate)
			{
				for (const VehiclePose& old : prev.Vehicles)
				{
					if (old.Body == vehicle.Body && old.Wheels.size() == vehicle.Wheels.size())
					{
						before = &old;
					}
				}
			}

			// the body now (the pose apply_poses just wrote, with the entity's scale) and as the WorldTransforms still have it (last frame):
			// a parent between the body and a wheel moves with the body, so it is where it was relative to the body
			const glm::mat4& bodyWorldBefore = registry.get<WorldTransform>(bodyEntity).Matrix;
			const Pose bodyBefore = decompose(bodyWorldBefore);
			const glm::mat4 bodyNowNoScale = glm::translate(glm::mat4(1.0f), now->second.first) * glm::mat4_cast(now->second.second);
			const glm::mat4 bodyWorldNow = bodyNowNoScale * glm::scale(glm::mat4(1.0f), bodyBefore.Scale);
			const glm::mat4 sinceBefore = bodyWorldNow * glm::inverse(bodyWorldBefore);

			for (size_t i = 0; i < vehicle.Wheels.size(); i++)
			{
				const WheelPose& wheel = vehicle.Wheels[i];
				const auto place = tracked->second.Wheels.find(wheel.Entity);
				const entt::entity entity = scene.find(wheel.Entity);
				if (place == tracked->second.Wheels.end() || entity == entt::null || !registry.valid(entity))
				{
					continue;
				}
				float suspension = wheel.SuspensionLength;
				float steer = wheel.SteerAngle;
				float spin = wheel.SpinAngle;
				if (before != nullptr)
				{
					const WheelPose& old = before->Wheels[i];
					suspension = glm::mix(old.SuspensionLength, wheel.SuspensionLength, alpha);
					steer = glm::mix(old.SteerAngle, wheel.SteerAngle, alpha);
					float delta = wheel.SpinAngle - old.SpinAngle;
					delta -= 6.28318531f * std::round(delta / 6.28318531f);
					spin = old.SpinAngle + delta * alpha;
				}

				// body space: up the suspension from where it was authored, spin about the body's x (rolling forward turns the top forward,
				// to -z: a negative turn about +x), then steer about the body's y
				const glm::vec3 position = place->second.Position + glm::vec3(0.0f, wheel.RestLength - suspension, 0.0f);
				const glm::quat rotation = glm::angleAxis(steer, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(-spin, glm::vec3(1.0f, 0.0f, 0.0f)) * place->second.Rotation;
				const glm::vec3 worldPosition = glm::vec3(bodyNowNoScale * glm::vec4(position, 1.0f));
				const glm::quat worldRotation = glm::normalize(now->second.second * rotation);

				Transform& transform = registry.get<Transform>(entity);
				const entt::entity parent = registry.get<Hierarchy>(entity).Parent;
				if (parent == entt::null)
				{
					continue;
				}
				const glm::mat4 parentNow = sinceBefore * registry.get<WorldTransform>(parent).Matrix;
				transform.Position = glm::vec3(glm::inverse(parentNow) * glm::vec4(worldPosition, 1.0f));
				transform.Rotation = glm::normalize(glm::inverse(decompose(parentNow).Rotation) * worldRotation);
			}
		}
	}

	PhysicsMaterialData PhysicsSync::resolve_material(AssetRef ref)
	{
		if (!ref.valid())
		{
			return default_physics_material();
		}

		const AssetGuid key = ref.resolved();
		auto it = m_Materials.find(key);
		if (it == m_Materials.end())
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (assets == nullptr)
			{
				return default_physics_material();
			}
			it = m_Materials.emplace(key, MaterialEntry{ assets->load<PhysicsMaterialData>(ref) }).first;
		}

		MaterialEntry& entry = it->second;
		switch (entry.Material.state())
		{
		case LoadState::Ready:
			if (const PhysicsMaterialData* data = entry.Material.get(); data != nullptr && physics_material_valid(*data))
			{
				return *data;
			}
			[[fallthrough]];
		case LoadState::Failed:
			if (!entry.Warned)
			{
				entry.Warned = true;
				WI_PHYSICS_LOG(Warning, "Physics material {0} is missing or could not be loaded: the default material is used", ref.Guid.to_string());
			}
			return default_physics_material();
		default:
			return default_physics_material(); // still loading
		}
	}

	std::shared_ptr<const CookedShapeData> PhysicsSync::resolve_shape(AssetRef mesh, CookedShapeKind kind, EntityGuid collider)
	{
		const auto warn = [&](const char* text)
		{
			if (m_WarnedColliders.insert(collider).second)
			{
				WI_PHYSICS_LOG(Warning, "Collider {0}: {1}; it does not collide", collider.to_string(), text);
			}
			return nullptr;
		};
		if (!mesh.valid())
		{
			return warn("a ConvexHull or Mesh collider needs a Mesh");
		}

		const AssetRef shapeRef = cooked_shape_ref(mesh, kind);
		const AssetGuid key = shapeRef.resolved();
		auto it = m_Shapes.find(key);
		if (it == m_Shapes.end())
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (assets == nullptr)
			{
				return nullptr;
			}
			it = m_Shapes.emplace(key, ShapeEntry{ assets->load<CookedShapeData>(shapeRef), nullptr }).first;
		}

		ShapeEntry& entry = it->second;
		switch (entry.Shape.state())
		{
		case LoadState::Ready:
			if (entry.Data == nullptr && entry.Shape.get() != nullptr)
			{
				entry.Data = std::make_shared<const CookedShapeData>(*entry.Shape.get());
			}
			return entry.Data;
		case LoadState::Failed:
			return warn("the mesh has no cooked physics shape (import the model with Generate Physics Shapes on)");
		default:
			return nullptr; // still loading
		}
	}

	void PhysicsSync::clear()
	{
		m_Bodies.clear();
		m_Materials.clear();
		m_Shapes.clear();
		m_WarnedColliders.clear();
		m_Joints.clear();
		m_WarnedJoints.clear();
		m_Characters.clear();
		m_WarnedCharacters.clear();
		m_Vehicles.clear();
		m_VehicleSettings.clear();
		m_WarnedVehicles.clear();
		m_VehiclesSent = 0;
	}
}
