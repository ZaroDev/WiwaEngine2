#pragma once

#include <math/math.h>

namespace wiwa
{
	// Every asset type with the CPU-side data it loads as, in the order of the enum below. The enum is written to files:
	// a new type goes at the END of this list (X(Name, DataType)), never in the middle. asset_type_name() and the
	// AssetManager's deserializer are generated from it.
#define WI_ASSET_TYPES(X) \
	X(Texture, TextureData) \
	X(Mesh, MeshData) \
	X(Material, MaterialData) \
	X(Skeleton, SkeletonData) \
	X(AnimationClip, AnimationClipData) \
	X(Prefab, PrefabData) \
	X(Environment, EnvironmentData) \
	X(Lut, LutData) \
	X(Shader, ShaderData) \
	X(Scene, SceneData) \
	X(Script, ScriptData) \
	X(SurfaceShader, SurfaceShaderData) \
	X(Font, FontData) \
	X(UiPanel, UiPanelData) \
	X(PhysicsMaterial, PhysicsMaterialData) \
	X(PhysicsShape, CookedShapeData) \
	X(InputMap, InputMapData) \
	X(AnimationGraph, AnimationGraphData) \
	X(ParticleEffect, ParticleEffectData) \
	X(SoundBank, SoundBankData) \
	X(AudioClip, AudioClipData) \
	X(NavMesh, NavMeshData) \
	X(BehaviorTree, BehaviorTreeData) \
	X(Video, VideoData)

	enum class AssetType : u32
	{
		None = 0,
#define WI_ASSET_TYPE_ENUM(name, data) name,
		WI_ASSET_TYPES(WI_ASSET_TYPE_ENUM)
#undef WI_ASSET_TYPE_ENUM

		Last
	};

	inline const char* asset_type_name(AssetType type)
	{
		switch (type)
		{
#define WI_ASSET_TYPE_NAME(name, data) case AssetType::name: return #name;
		WI_ASSET_TYPES(WI_ASSET_TYPE_NAME)
#undef WI_ASSET_TYPE_NAME
		default: return "None";
		}
	}

	enum class LoadState : u32
	{
		Loading,
		Ready,
		Failed
	};
}
