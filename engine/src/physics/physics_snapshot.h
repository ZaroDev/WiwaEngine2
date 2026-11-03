#pragma once

#include <vector>

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

	enum class PhysicsEventKind : u32 { CollisionEnter, CollisionStay, CollisionExit, TriggerEnter, TriggerExit };

	// A contact or trigger event of one step, made by the contact listener on the physics thread. A and B name the two bodies (the
	// owning bodies: a compound's part reports as its body), A < B. A pair where either body is a trigger gives Trigger events only.
	//  - Enter: the first contact of the pair (a pair touching through several parts of a compound gives one); Exit: its last contact
	//    ended, or one of the two bodies was destroyed. A contact lost because the bodies fell asleep is not an exit.
	//  - Stay: at most one per pair per step, and only when either body has RigidBody::ReportContacts (none in the step of the Enter).
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

	class QuerySnapshot;

	// What the physics thread publishes after each step. Immutable once published; the main thread never reads Jolt structures.
	struct PhysicsStepSnapshot
	{
		u64 World = 0;       // the generation of the world that published it (PhysicsThread::generation); a new world never shows an older one's
		u64 StepIndex = 0;   // steps completed since the world started (0: the world before its first step)
		double Time = 0.0;   // simulated seconds since the world started
		float FixedDt = 0.0f; // the length of the step that produced this snapshot
		std::vector<BodyPose> Poses{};
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
		float StepMs = 0.0f;    // wall time of PhysicsSystem::Update
	};
}
