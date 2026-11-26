#pragma once

#include <vector>

#include "physics_commands.h"
#include "physics_types.h"

namespace wiwa
{
	// The pose of one body after a step.
	struct BodyPose
	{
		BodyKey Key{};
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 LinearVelocity{ 0.0f };
		glm::vec3 AngularVelocity{ 0.0f };
		bool Active = false; // awake (not sleeping)
	};

	enum class PhysicsEventKind : u32 { CollisionEnter, CollisionStay, CollisionExit, TriggerEnter, TriggerExit, JointBreak };

	// A contact or trigger event of one step, made by the contact listener on the physics thread. A and B name the two bodies (the
	// owning bodies: a compound's part reports as its body), A < B. A pair where either body is a trigger gives Trigger events only.
	//  - Enter: the first contact of the pair (a pair touching through several parts of a compound gives one); Exit: its last contact
	//    ended, or one of the two bodies was destroyed. A contact lost because the bodies fell asleep is not an exit.
	//  - Stay: at most one per pair per step, and only when either body has RigidBody::ReportContacts (none in the step of the Enter).
	// JointBreak: a joint went over its break force or torque and was removed; A is the entity that owns the Joint, B the entity it was
	// connected to (a null guid: the world). Not ordered A < B, delivered like begin and end events (never lost).
	// Point, Normal, Impulse and RelativeVelocity are set on CollisionEnter and CollisionStay (zero on the others):
	//  - Point: the middle of the contact points, world space; Normal: unit, pointing from A towards B (the way B is pushed out of A);
	//  - RelativeVelocity: the velocity of B minus that of A at Point, before the step resolved the contact (m/s);
	//  - Impulse: Jolt's estimate of the normal impulse of the contact (EstimateCollisionResponse, kg m/s); the solver's actual impulse
	//    is not known to the listener. Not set for triggers.
	struct PhysicsEvent
	{
		PhysicsEventKind Kind = PhysicsEventKind::CollisionEnter;
		BodyKey A{};
		BodyKey B{};
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		float Impulse = 0.0f;
		glm::vec3 RelativeVelocity{ 0.0f };
	};

	// The colliders as Jolt's debug renderer draws them, in world space. Colors are Jolt's packed 0xAABBGGRR.
	struct PhysicsDebugLine
	{
		glm::vec3 A{ 0.0f };
		glm::vec3 B{ 0.0f };
		u32 Color = 0;
	};

	struct PhysicsDebugTriangle
	{
		glm::vec3 A{ 0.0f };
		glm::vec3 B{ 0.0f };
		glm::vec3 C{ 0.0f };
		u32 Color = 0;
	};

	struct PhysicsDebugDraw
	{
		std::vector<PhysicsDebugLine> Lines{};
		std::vector<PhysicsDebugTriangle> Triangles{};
	};

	// How a character stands: Grounded (on ground it can walk on), Sliding (on a slope steeper than its MaxSlopeAngle) or Airborne.
	enum class GroundState : u32 { Grounded, Sliding, Airborne };

	// The state of one character after a step.
	struct CharacterPose
	{
		BodyKey Key{};
		glm::vec3 Position{ 0.0f };     // of the entity (the capsule is Offset from it)
		glm::vec3 Velocity{ 0.0f };
		GroundState Ground = GroundState::Airborne;
		BodyKey GroundBody{};           // the entity it stands on (a null key: none, or a body that is not an entity's)
		glm::vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
	};

	// The state of one wheel of a vehicle after a step.
	struct WheelPose
	{
		BodyKey Entity{};
		float SteerAngle = 0.0f;       // radians, about the body's +y (+: left)
		float SpinAngle = 0.0f;        // radians about the axle, [-pi, pi]; grows while rolling forward
		float SuspensionLength = 0.0f; // m, from the attachment point down to the wheel centre
		float RestLength = 0.0f;       // SuspensionLength at rest: where the wheel was authored (the wheel moves up by RestLength - SuspensionLength)
		bool InContact = false;
		BodyKey ContactBody{};         // the entity it touches (a null key: none, or a body that is not an entity's)
		float Grip = 0.0f;             // the TireGrip of what it touches
		float SlipRatio = 0.0f;        // (wheel speed - ground speed) / ground speed, ground speed at least 1 m/s
		float SlipAngle = 0.0f;        // radians, + when the contact slides right
		float Load = 0.0f;             // N
		float AligningTorque = 0.0f;   // Nm
		float AngularVelocity = 0.0f;  // rad/s, + rolling forward
	};

	enum VehicleAssistActive : u32 { AssistAbs = 1u, AssistTc = 2u, AssistStability = 4u };

	// The state of one vehicle after a step.
	struct VehiclePose
	{
		BodyKey Body{};
		float Speed = 0.0f;      // m/s along the body's forward (-z), signed
		float EngineRpm = 0.0f;
		i32 Gear = 0;            // 0 neutral, below 0 reverse
		float Clutch = 0.0f;     // 0..1
		VehicleInput Applied{};  // what the vehicle got, after the assists
		u32 AssistsActive = 0;   // VehicleAssistActive bits
		std::vector<WheelPose> Wheels{}; // in VehicleDesc order
	};

	class QuerySnapshot;

	// What the physics thread publishes after each step. Immutable once published; the main thread never reads Jolt structures.
	struct PhysicsStepSnapshot
	{
		u64 World = 0;       // the generation of the world that published it (PhysicsThread::generation); a new world never shows an older one's
		u64 StepIndex = 0;   // steps completed since the world started (0: the world before its first step)
		double Time = 0.0;   // simulated seconds since the world started
		float FixedDt = 0.0f; // the length of the step that produced this snapshot
		std::vector<BodyPose> Poses{};
		std::vector<CharacterPose> Characters{};
		std::vector<VehiclePose> Vehicles{};
		// This step's events, sorted: first those made by the commands applied before the step (a destroyed body's exits), then the
		// step's own, each group by (Kind, A, B). When the main thread fell far behind, a snapshot can also carry the begin and end events
		// of older steps that were folded into it (PhysicsThread::take_steps), before its own.
		std::vector<PhysicsEvent> Events{};
		// The world as this step left it, for the queries (raycast, overlap, shape cast) of the step's handlers. Null in the snapshot a world
		// publishes before its first step.
		Ref<const QuerySnapshot> Query{};
		// What the debug renderer drew for this step; null unless the world was asked to (SetDebugDraw), and always null in Shipping.
		Ref<const PhysicsDebugDraw> Debug{};
		// What the step cost and how big the world is, for the editor's Physics panel.
		u32 BodyCount = 0;
		u32 ActiveBodies = 0;   // awake (not sleeping)
		u32 JointCount = 0;     // joints that are made (not waiting for a body)
		u32 CharacterCount = 0;
		u32 VehicleCount = 0;   // vehicles that are made (not waiting for a body)
		float StepMs = 0.0f;    // wall time of PhysicsSystem::Update
	};
}
