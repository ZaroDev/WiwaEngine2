#include <pch.h>
#include "physics_world.h"
#include "jolt_config.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <vector>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "contact_listener.h"
#include "physics_layers_jolt.h"
#include "physics_material_jolt.h"
#include "query_snapshot_jolt.h"
#include "shape_builder.h"

namespace wiwa
{
	namespace
	{
		constexpr JPH::uint kMaxBodies = 65536;
		constexpr JPH::uint kMaxBodyPairs = 65536;
		constexpr JPH::uint kMaxContactConstraints = 10240;
		constexpr size_t kTempAllocatorBytes = 10 * 1024 * 1024;
		constexpr u32 kMaxFixedRateHz = 1000;

		template<typename>
		inline constexpr bool always_false = false;

		u32 worker_count()
		{
			const int hardware = static_cast<int>(std::thread::hardware_concurrency());
			return static_cast<u32>(std::clamp(hardware - 2, 1, 8)); // leave room for the main and render threads
		}

		PhysicsSettings sanitized(PhysicsSettings settings)
		{
			settings.FixedRateHz = std::clamp<u32>(settings.FixedRateHz, 1, kMaxFixedRateHz);
			settings.MaxStepsPerWake = std::max<u32>(settings.MaxStepsPerWake, 1);
			settings.VelocityIterations = std::max<u32>(settings.VelocityIterations, 1);
			settings.PositionIterations = std::max<u32>(settings.PositionIterations, 1);
			settings.SleepThreshold = std::max(settings.SleepThreshold, 0.0f);
			return settings;
		}

		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		JPH::RVec3 to_jolt(glm::vec3 v) { return finite(v) ? JPH::RVec3(v.x, v.y, v.z) : JPH::RVec3::sZero(); }
		JPH::Quat to_jolt(glm::quat q)
		{
			const float length = glm::length(q);
			if (!(length > 1e-6f) || !std::isfinite(length))
			{
				return JPH::Quat::sIdentity();
			}
			q /= length;
			return JPH::Quat(q.x, q.y, q.z, q.w);
		}
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		glm::quat to_glm(JPH::Quat q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		JPH::EMotionType to_jolt(MotionType motion)
		{
			switch (motion)
			{
			case MotionType::Static: return JPH::EMotionType::Static;
			case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
			default: return JPH::EMotionType::Dynamic;
			}
		}

		JPH::EAllowedDOFs allowed_dofs(const BodyDesc& desc)
		{
			u32 dofs = static_cast<u32>(JPH::EAllowedDOFs::All);
			const JPH::EAllowedDOFs translation[3] = { JPH::EAllowedDOFs::TranslationX, JPH::EAllowedDOFs::TranslationY, JPH::EAllowedDOFs::TranslationZ };
			const JPH::EAllowedDOFs rotation[3] = { JPH::EAllowedDOFs::RotationX, JPH::EAllowedDOFs::RotationY, JPH::EAllowedDOFs::RotationZ };
			for (u32 axis = 0; axis < 3; axis++)
			{
				if (desc.LockPosition[axis])
				{
					dofs &= ~static_cast<u32>(translation[axis]);
				}
				if (desc.LockRotation[axis])
				{
					dofs &= ~static_cast<u32>(rotation[axis]);
				}
			}
			return static_cast<JPH::EAllowedDOFs>(dofs);
		}

		// Everything Jolt that one world needs. Created, used and destroyed on the physics thread, never seen by any other.
		class PhysicsWorld final : public IPhysicsWorld
		{
		public:
			PhysicsWorld(const PhysicsSettings& settings, const PhysicsLayers& layers) : m_Pairs(layers)
			{
				m_System.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, m_BroadPhase, m_ObjectVsBroad, m_Pairs);
					m_Contacts.set_bodies(&m_System.GetBodyInterfaceNoLock());
					m_System.SetContactListener(&m_Contacts);
				apply(settings);
				// the friction and restitution of a contact come from the two shapes' materials (physics_material_jolt.h), not from the bodies
				m_System.SetCombineFriction(&combine_friction);
				m_System.SetCombineRestitution(&combine_restitution);
				m_Placeholder = new JPH::SphereShape(ShapeBuilder::MIN_SHAPE_SIZE);
			}

			~PhysicsWorld() override
			{
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				for (const auto& [key, entry] : m_Bodies)
				{
					bodies.RemoveBody(entry.Id);
					bodies.DestroyBody(entry.Id);
				}
			}

			void execute(const PhysicsCommand& command) override
			{
				std::visit([this](const auto& c)
				{
					using T = std::decay_t<decltype(c)>;
					if constexpr (std::is_same_v<T, SetSettings>)
					{
						apply(c.Settings);
					}
					else if constexpr (std::is_same_v<T, CreateBody>)
					{
						create_body(c.Key, c.Desc);
					}
					else if constexpr (std::is_same_v<T, DestroyBody>)
					{
						destroy_body(c.Key);
					}
					else if constexpr (std::is_same_v<T, SetTransform>)
					{
						set_transform(c.Key, c.Pos, c.Rot);
					}
					else if constexpr (std::is_same_v<T, SetKinematicTarget>)
					{
						set_kinematic_target(c.Key, c.Pos, c.Rot);
					}
					else if constexpr (std::is_same_v<T, SetMaterial>)
					{
						set_material(c.Key, c.Material);
					}
					else if constexpr (std::is_same_v<T, ReplaceShape>)
					{
						replace_shape(c.Key, c.Shape);
					}
					else if constexpr (std::is_same_v<T, SetLayer>)
					{
						set_layer(c.Key, c.Layer);
					}
					else if constexpr (std::is_same_v<T, SetLayers>)
					{
						set_layers(c.Layers);
					}
					else
					{
						static_assert(always_false<T>, "a PhysicsCommand kind is not handled by PhysicsWorld::execute");
					}
				}, command);
			}

			void step() override
			{
				m_Contacts.begin_step();
				const JPH::EPhysicsUpdateError error = m_System.Update(static_cast<float>(fixed_dt()), 1, &m_Temp, &m_Jobs);
				if (error != JPH::EPhysicsUpdateError::None)
				{
					WI_PHYSICS_LOG(Warning, "Physics step reported error flags {0}", static_cast<u32>(error));
				}

				// a kinematic body reached its target: it stops there until it gets another
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				for (const JPH::BodyID id : m_MovedKinematic)
				{
					bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				}
				m_MovedKinematic.clear();
				m_Contacts.end_step();
			}

			void write_poses(PhysicsStepSnapshot& snapshot) const override
			{
				const JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				snapshot.Poses.reserve(m_Bodies.size());
				for (const auto& [key, entry] : m_Bodies)
				{
					JPH::RVec3 position;
					JPH::Quat rotation;
					bodies.GetPositionAndRotation(entry.Id, position, rotation);
					BodyPose pose;
					pose.Key = key;
					pose.Position = to_glm(position);
					pose.Rotation = to_glm(rotation);
					pose.LinearVelocity = to_glm(bodies.GetLinearVelocity(entry.Id));
					pose.AngularVelocity = to_glm(bodies.GetAngularVelocity(entry.Id));
					pose.Active = bodies.IsActive(entry.Id);
					snapshot.Poses.push_back(pose);
				}
			}

			void take_events(PhysicsStepSnapshot& snapshot) override { m_Contacts.take_events(snapshot.Events); }

			void write_query(PhysicsStepSnapshot& snapshot) override
			{
				const JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				const auto describe = [&](BodyKey key, const BodyEntry& entry)
				{
					QueryBody body;
					body.Key = key;
					body.Shape = entry.Built;
					JPH::RVec3 position;
					JPH::Quat rotation;
					bodies.GetPositionAndRotation(entry.Id, position, rotation);
					body.Position = to_glm(position);
					body.Rotation = to_glm(rotation);
					body.Layer = static_cast<u32>(std::clamp<i32>(entry.Layer, 0, static_cast<i32>(PHYSICS_LAYER_COUNT) - 1));
					return body;
				};

				// bodies that collide with nothing (triggers, placeholders) are not queried; the static ones are kept until one changes
				if (m_StaticDirty || m_StaticQuery == nullptr)
				{
					auto statics = std::make_shared<std::vector<QueryBody>>();
					for (const auto& [key, entry] : m_Bodies)
					{
						if (entry.Motion == MotionType::Static && entry.Built != nullptr && !entry.IsTrigger)
						{
							statics->push_back(describe(key, entry));
						}
					}
					m_StaticQuery = std::move(statics);
					m_StaticDirty = false;
				}

				auto impl = std::make_unique<QuerySnapshot::Impl>();
				impl->Static = m_StaticQuery;
				for (const auto& [key, entry] : m_Bodies)
				{
					if (entry.Motion != MotionType::Static && entry.Built != nullptr && !entry.IsTrigger)
					{
						impl->Moving.push_back(describe(key, entry));
					}
				}
				snapshot.Query = std::make_shared<QuerySnapshot>(std::move(impl));
			}

			double fixed_dt() const override { return 1.0 / static_cast<double>(m_Settings.FixedRateHz); }
			u32 max_steps_per_wake() const override { return m_Settings.MaxStepsPerWake; }

		private:
			struct BodyEntry
			{
				JPH::BodyID Id{};
				MotionType Requested = MotionType::Static; // what the RigidBody asks for
				MotionType Motion = MotionType::Static;    // what the body is (a placeholder or a fully locked body does not move by itself)
				JPH::EAllowedDOFs Dofs = JPH::EAllowedDOFs::All;
				float MassOverride = 0.0f;                 // 0: from the shape
				i32 Layer = 0;
				bool IsTrigger = false;
				bool ReportContacts = false;
				bool Placeholder = false;                  // no collidable shape: it does not collide
				JPH::RefConst<JPH::Shape> Built{};         // the Jolt shape (null: the placeholder), for the query snapshot
				ShapeDesc Shape{};                        // as last built, for SetMaterial
			};

			enum class BodyWarning : u32 { AllLocked, Placeholder, MustBeStatic };

			// A triangle mesh cannot move: a body that asks to is left without a shape (it becomes a non-colliding placeholder).
			JPH::RefConst<JPH::Shape> movable_or_null(BodyKey key, MotionType requested, JPH::RefConst<JPH::Shape> shape)
			{
				if (shape != nullptr && requested != MotionType::Static && shape->MustBeStatic())
				{
					warn_once(key, BodyWarning::MustBeStatic, "a Mesh collider cannot be on a moving body (use a ConvexHull); it does not collide");
					return nullptr;
				}
				return shape;
			}

			void apply(const PhysicsSettings& settings)
			{
				m_Settings = sanitized(settings);
				m_System.SetGravity(JPH::Vec3(m_Settings.Gravity.x, m_Settings.Gravity.y, m_Settings.Gravity.z));
				JPH::PhysicsSettings jolt = m_System.GetPhysicsSettings();
				jolt.mNumVelocitySteps = m_Settings.VelocityIterations;
				jolt.mNumPositionSteps = m_Settings.PositionIterations;
				jolt.mPointVelocitySleepThreshold = m_Settings.SleepThreshold;
				m_System.SetPhysicsSettings(jolt);
			}

			void warn_once(BodyKey key, BodyWarning warning, const std::string& text)
			{
				if (m_Warned.insert({ key, static_cast<u32>(warning) }).second)
				{
					WI_PHYSICS_LOG(Warning, "Body {0}: {1}", key.to_string(), text);
				}
			}

			// A body that cannot move by itself: a placeholder (no shape) and a dynamic body with every axis locked do not fall.
			MotionType effective_motion(BodyKey key, MotionType requested, bool placeholder, JPH::EAllowedDOFs dofs)
			{
				if (requested != MotionType::Dynamic)
				{
					return requested;
				}
				if (placeholder)
				{
					warn_once(key, BodyWarning::Placeholder, "it has no collidable shape: it does not collide and does not fall");
					return MotionType::Kinematic;
				}
				if (dofs == JPH::EAllowedDOFs::None)
				{
					warn_once(key, BodyWarning::AllLocked, "every axis is locked: it is made kinematic");
					return MotionType::Kinematic;
				}
				return requested;
			}

			static BodyCategory category(const BodyEntry& entry, bool placeholder)
			{
				if (placeholder)
				{
					return BodyCategory::NoCollision;
				}
				if (entry.IsTrigger)
				{
					return BodyCategory::Trigger;
				}
				return entry.Motion == MotionType::Static ? BodyCategory::Static : BodyCategory::Moving;
			}

			void create_body(BodyKey key, const BodyDesc& desc)
			{
				destroy_body(key); // a CreateBody for a key that has a body replaces it

				JPH::RefConst<JPH::Shape> shape = movable_or_null(key, desc.Motion, m_Shapes.build(desc.Shape));
				const bool placeholder = shape == nullptr;
				const JPH::EAllowedDOFs dofs = allowed_dofs(desc);

				BodyEntry entry;
				entry.Requested = desc.Motion;
				entry.Motion = effective_motion(key, desc.Motion, placeholder, dofs);
				entry.Dofs = dofs;
				entry.MassOverride = std::isfinite(desc.MassOverride) ? std::max(desc.MassOverride, 0.0f) : 0.0f; // negative: treated as 0
				entry.Layer = desc.Layer;
				entry.IsTrigger = desc.IsTrigger;
				entry.ReportContacts = desc.ReportContacts;
				entry.Placeholder = placeholder;
				entry.Built = shape;
				entry.Shape = desc.Shape;

				JPH::BodyCreationSettings settings(placeholder ? m_Placeholder.GetPtr() : shape.GetPtr(), to_jolt(desc.Position), to_jolt(desc.Rotation),
					to_jolt(entry.Motion), to_object_layer(desc.Layer, category(entry, placeholder)));
				settings.mAllowDynamicOrKinematic = entry.Requested != MotionType::Static; // a placeholder may become dynamic once it has a shape
				settings.mLinearDamping = std::isfinite(desc.LinearDamping) ? std::max(desc.LinearDamping, 0.0f) : 0.0f;
				settings.mAngularDamping = std::isfinite(desc.AngularDamping) ? std::max(desc.AngularDamping, 0.0f) : 0.0f;
				settings.mGravityFactor = std::isfinite(desc.GravityFactor) ? desc.GravityFactor : 1.0f;
				settings.mMotionQuality = desc.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
				settings.mAllowSleeping = desc.AllowSleeping;
				settings.mIsSensor = desc.IsTrigger;
				if (entry.Requested == MotionType::Dynamic && dofs != JPH::EAllowedDOFs::None)
				{
					settings.mAllowedDOFs = dofs; // also for a placeholder that is kinematic for now: the locks hold once it is dynamic
				}
				if (entry.MassOverride > 0.0f)
				{
					settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					settings.mMassPropertiesOverride.mMass = entry.MassOverride;
				}

				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				entry.Id = bodies.CreateAndAddBody(settings, entry.Motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
				if (entry.Id.IsInvalid())
				{
					WI_PHYSICS_LOG(Error, "Body {0} could not be made: the world is full ({1} bodies)", key.to_string(), kMaxBodies);
					return;
				}
				m_Bodies[key] = entry;
				m_StaticDirty = true;
				m_Contacts.register_body(entry.Id, key, desc.IsTrigger, desc.ReportContacts);
			}

			void destroy_body(BodyKey key)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				m_Contacts.unregister_body(it->second.Id);
				m_StaticDirty = true;
				bodies.RemoveBody(it->second.Id);
				bodies.DestroyBody(it->second.Id);
				std::erase(m_MovedKinematic, it->second.Id);
				m_Bodies.erase(it);
			}

			void set_transform(BodyKey key, glm::vec3 position, glm::quat rotation)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				m_StaticDirty = m_StaticDirty || it->second.Motion == MotionType::Static;
				const JPH::EActivation activation = it->second.Motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
				m_System.GetBodyInterfaceNoLock().SetPositionAndRotation(it->second.Id, to_jolt(position), to_jolt(rotation), activation);
			}

			void set_kinematic_target(BodyKey key, glm::vec3 position, glm::quat rotation)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				if (it->second.Motion != MotionType::Kinematic)
				{
					if (it->second.Motion == MotionType::Static)
					{
						set_transform(key, position, rotation);
					}
					return; // a dynamic body moves by itself
				}
				m_System.GetBodyInterfaceNoLock().MoveKinematic(it->second.Id, to_jolt(position), to_jolt(rotation), static_cast<float>(fixed_dt()));
				if (std::find(m_MovedKinematic.begin(), m_MovedKinematic.end(), it->second.Id) == m_MovedKinematic.end())
				{
					m_MovedKinematic.push_back(it->second.Id);
				}
			}

			// Every part of the body's shape gets the material, then the shape is rebuilt like ReplaceShape does (the density changes the mass).
			void set_material(BodyKey key, const PhysicsMaterialData& material)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				ShapeDesc shape = it->second.Shape;
				shape.Material = material;
				for (ShapeDesc& child : shape.Children)
				{
					child.Material = material;
				}
				replace_shape(key, shape);
			}

			void replace_shape(BodyKey key, const ShapeDesc& desc)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				BodyEntry& entry = it->second;
				entry.Shape = desc;
				m_StaticDirty = true;
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();

				JPH::RefConst<JPH::Shape> shape = movable_or_null(key, entry.Requested, m_Shapes.build(desc));
				const bool placeholder = shape == nullptr;
				entry.Placeholder = placeholder;
				entry.Built = shape;
				const MotionType motion = effective_motion(key, entry.Requested, placeholder, entry.Dofs);
				const bool moving = motion != MotionType::Static;
				const JPH::Shape* newShape = placeholder ? m_Placeholder.GetPtr() : shape.GetPtr();

				// the shape first, the mass from it (scaled to the override, which Jolt's own update would lose), then the motion type
				bodies.SetShape(entry.Id, newShape, false, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
				if (entry.Requested == MotionType::Dynamic)
				{
					JPH::MassProperties mass = newShape->GetMassProperties();
					if (entry.MassOverride > 0.0f)
					{
						mass.ScaleToMass(entry.MassOverride);
					}
					JPH::BodyLockWrite lock(m_System.GetBodyLockInterfaceNoLock(), entry.Id);
					if (lock.Succeeded() && lock.GetBody().GetMotionPropertiesUnchecked() != nullptr)
					{
						lock.GetBody().GetMotionPropertiesUnchecked()->SetMassProperties(entry.Dofs != JPH::EAllowedDOFs::None ? entry.Dofs : JPH::EAllowedDOFs::All, mass);
					}
				}
				if (motion != entry.Motion)
				{
					bodies.SetMotionType(entry.Id, to_jolt(motion), JPH::EActivation::Activate);
					bodies.SetLinearAndAngularVelocity(entry.Id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
					entry.Motion = motion;
				}
				bodies.SetObjectLayer(entry.Id, to_object_layer(entry.Layer, category(entry, placeholder)));
			}

			void set_layer(BodyKey key, i32 layer)
			{
				const auto it = m_Bodies.find(key);
				if (it == m_Bodies.end())
				{
					return;
				}
				BodyEntry& entry = it->second;
				entry.Layer = layer;
				m_StaticDirty = true;
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				bodies.SetObjectLayer(entry.Id, to_object_layer(layer, category(entry, entry.Placeholder)));
				if (entry.Motion != MotionType::Static)
				{
					bodies.ActivateBody(entry.Id); // a sleeping body is not tested again by itself
				}
			}

			void set_layers(const PhysicsLayers& layers)
			{
				m_Pairs.set_layers(layers);
				JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
				for (const auto& [key, entry] : m_Bodies)
				{
					if (entry.Motion != MotionType::Static)
					{
						bodies.ActivateBody(entry.Id); // sleeping bodies test their pairs again
					}
				}
			}

			// the layer interfaces must outlive the system
			BroadPhaseLayers m_BroadPhase{};
			ObjectVsBroadPhase m_ObjectVsBroad{};
			ObjectLayerPairs m_Pairs;
			JPH::TempAllocatorImpl m_Temp{ kTempAllocatorBytes };
			JPH::JobSystemThreadPool m_Jobs{ JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(worker_count()) };
			JPH::PhysicsSystem m_System{};
			ContactCollector m_Contacts{};
			PhysicsSettings m_Settings{};

			ShapeBuilder m_Shapes{};
			JPH::RefConst<JPH::Shape> m_Placeholder{};
			std::unordered_map<BodyKey, BodyEntry> m_Bodies{};
			std::vector<JPH::BodyID> m_MovedKinematic{};
			std::shared_ptr<const std::vector<QueryBody>> m_StaticQuery{}; // the static bodies of the query snapshots, until one changes
			bool m_StaticDirty = true;
			std::set<std::pair<BodyKey, u32>> m_Warned{};
		};
	}

	Scope<IPhysicsWorld> create_physics_world(const PhysicsSettings& settings, const PhysicsLayers& layers)
	{
		return CreateScope<PhysicsWorld>(settings, layers);
	}
}
