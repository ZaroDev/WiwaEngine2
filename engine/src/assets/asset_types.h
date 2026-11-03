#pragma once

#include <math/math.h>

namespace wiwa
{
	enum class AssetType : u32
	{
		None = 0,
		Texture,
		Mesh,
		Material,
		Skeleton,
		AnimationClip,
		Prefab,
		Environment,
		Lut,
		Shader,
		Scene,
		Script,
		SurfaceShader,
		Font,
		UiPanel,
		PhysicsMaterial,
		PhysicsShape,

		Last
	};

	inline const char* asset_type_name(AssetType type)
	{
		switch (type)
		{
		case AssetType::Texture: return "Texture";
		case AssetType::Mesh: return "Mesh";
		case AssetType::Material: return "Material";
		case AssetType::Skeleton: return "Skeleton";
		case AssetType::AnimationClip: return "AnimationClip";
		case AssetType::Prefab: return "Prefab";
		case AssetType::Environment: return "Environment";
		case AssetType::Lut: return "Lut";
		case AssetType::Shader: return "Shader";
		case AssetType::Scene: return "Scene";
		case AssetType::Script: return "Script";
		case AssetType::SurfaceShader: return "SurfaceShader";
		case AssetType::Font: return "Font";
		case AssetType::UiPanel: return "UiPanel";
		case AssetType::PhysicsMaterial: return "PhysicsMaterial";
		case AssetType::PhysicsShape: return "PhysicsShape";
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
