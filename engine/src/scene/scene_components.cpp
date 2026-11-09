#include <pch.h>
#include "scene_components.h"

#include "components.h"
#include "reflection/component_registry.h"
#include "reflection/reflect_macros.h"

namespace wiwa
{
	void register_scene_components()
	{
		ComponentRegistry& registry = ComponentRegistry::get();

		registry.register_component(WI_COMPONENT(Id, WI_FIELD(Guid)));
		registry.register_component(WI_COMPONENT(Name, WI_FIELD(Value)));
		registry.register_component(WI_COMPONENT(Transform, WI_FIELD(Position), WI_FIELD(Rotation), WI_FIELD(Scale)));

		registry.register_component(WI_COMPONENT(Camera,
			WI_FIELD_META(Projection, FieldMeta::enumeration({ "Perspective", "Orthographic" })),
			WI_FIELD_META(FovY, FieldMeta::range(1.0f, 179.0f)),
			WI_FIELD(Near),
			WI_FIELD(Far),
			WI_FIELD(OrthoSize),
			WI_FIELD(Primary)));

		registry.register_component(WI_COMPONENT(Light,
			WI_FIELD_META(Kind, FieldMeta::enumeration({ "Directional", "Point", "Spot" })),
			WI_FIELD_META(Color, FieldMeta::color()),
			WI_FIELD(Intensity),
			WI_FIELD(Range),
			WI_FIELD(InnerAngle),
			WI_FIELD(OuterAngle),
			WI_FIELD(CastsShadows)));

		registry.register_component(WI_COMPONENT(MeshRenderer,
			WI_FIELD_META(Mesh, FieldMeta::asset(AssetType::Mesh)),
			WI_FIELD_META(Materials, FieldMeta::asset(AssetType::Material))));

		// The overrides are not a field: the record layer saves and applies them.
		registry.register_component(WI_COMPONENT(PrefabInstance,
			WI_FIELD_META(Prefab, FieldMeta::asset(AssetType::Prefab))));

		registry.register_component(WI_COMPONENT(UICanvas,
			WI_FIELD_META(Panel, FieldMeta::asset(AssetType::UiPanel)),
			WI_FIELD(SortOrder),
			WI_FIELD(Visible)));

		registry.register_component(WI_COMPONENT(RigidBody,
			WI_FIELD_META(Motion, FieldMeta::enumeration({ "Static", "Kinematic", "Dynamic" })),
			WI_FIELD(MassOverride),
			WI_FIELD(LinearDamping),
			WI_FIELD(AngularDamping),
			WI_FIELD(GravityFactor),
			WI_FIELD(ContinuousCollision),
			WI_FIELD(AllowSleeping),
			WI_FIELD(ReportContacts),
			WI_FIELD(LockPositionX),
			WI_FIELD(LockPositionY),
			WI_FIELD(LockPositionZ),
			WI_FIELD(LockRotationX),
			WI_FIELD(LockRotationY),
			WI_FIELD(LockRotationZ)));

		registry.register_component(WI_COMPONENT(Collider,
			WI_FIELD_META(Shape, FieldMeta::enumeration({ "Box", "Sphere", "Capsule", "Cylinder", "ConvexHull", "Mesh" })),
			WI_FIELD(Size),
			WI_FIELD(Offset),
			WI_FIELD(RotationEuler),
			WI_FIELD(IsTrigger),
			WI_FIELD_META(Layer, FieldMeta::range(0.0f, 31.0f)), // PHYSICS_LAYER_COUNT layers; the physics world clamps whatever is stored
			WI_FIELD_META(Material, FieldMeta::asset(AssetType::PhysicsMaterial)),
			WI_FIELD_META(Mesh, FieldMeta::asset(AssetType::Mesh))));

		registry.register_component(WI_COMPONENT(Joint,
			WI_FIELD_META(Type, FieldMeta::enumeration({ "Fixed", "Point", "Hinge", "Slider", "Distance", "Cone", "SwingTwist", "SixDof" })),
			WI_FIELD(Connected),
			WI_FIELD(CollideConnected),
			WI_FIELD(AnchorA),
			WI_FIELD(AnchorB),
			WI_FIELD(Axis),
			WI_FIELD(NormalAxis),
			WI_FIELD(LimitMin),
			WI_FIELD(LimitMax),
			WI_FIELD_META(SwingHalfAngle, FieldMeta::range(0.0f, 180.0f)),
			WI_FIELD_META(TwistHalfAngle, FieldMeta::range(0.0f, 180.0f)),
			WI_FIELD(SpringFrequency),
			WI_FIELD(SpringDamping),
			WI_FIELD_META(MotorMode, FieldMeta::enumeration({ "Off", "Velocity", "Position" })),
			WI_FIELD(MotorTarget),
			WI_FIELD(MotorMaxForce),
			WI_FIELD(BreakForce),
			WI_FIELD(BreakTorque)));

		registry.register_component(WI_COMPONENT(CharacterController,
			WI_FIELD(Radius),
			WI_FIELD(Height),
			WI_FIELD(Offset),
			WI_FIELD(StepHeight),
			WI_FIELD_META(MaxSlopeAngle, FieldMeta::range(0.0f, 89.0f)),
			WI_FIELD(Mass),
			WI_FIELD(PushStrength),
			WI_FIELD(GravityFactor),
			WI_FIELD_META(Layer, FieldMeta::range(0.0f, 31.0f))));

		registry.register_component(WI_COMPONENT(Animator,
			WI_FIELD_META(Skeleton, FieldMeta::asset(AssetType::Skeleton)),
			WI_FIELD_META(Clips, FieldMeta::asset(AssetType::AnimationClip)),
			WI_FIELD_META(Clip, FieldMeta::asset(AssetType::AnimationClip)),
			WI_FIELD(PlayOnStart),
			WI_FIELD(Speed),
			WI_FIELD(Enabled),
			WI_FIELD_META(BoundsPadding, FieldMeta::range(0.0f, 100.0f)),
			WI_FIELD_META(Graph, FieldMeta::asset(AssetType::AnimationGraph))));

		registry.register_component(WI_COMPONENT(AnimationSocket,
			WI_FIELD(Joint),
			WI_FIELD(OffsetPosition),
			WI_FIELD(OffsetRotation),
			WI_FIELD(OffsetScale)));

		registry.register_component(WI_COMPONENT(ViewHost,
			WI_FIELD(Name),
			WI_FIELD_META(Model, FieldMeta::asset(AssetType::Prefab)),
			WI_FIELD(Target),
			WI_FIELD_META(Distance, FieldMeta::range(0.1f, 200.0f)),
			WI_FIELD(Yaw),
			WI_FIELD_META(Pitch, FieldMeta::range(-89.0f, 89.0f)),
			WI_FIELD_META(Fov, FieldMeta::range(1.0f, 120.0f)),
			WI_FIELD(SpinSpeed),
			WI_FIELD_META(Width, FieldMeta::range(1.0f, 4096.0f)),
			WI_FIELD_META(Height, FieldMeta::range(1.0f, 4096.0f)),
			WI_FIELD_META(Profile, FieldMeta::enumeration({ "Full", "Preview", "Thumbnail" })),
			WI_FIELD(Transparent),
			WI_FIELD(Continuous)));

		registry.register_component(WI_COMPONENT(SkinBinding,
			WI_FIELD_META(Skeleton, FieldMeta::asset(AssetType::Skeleton))));
	}
}
