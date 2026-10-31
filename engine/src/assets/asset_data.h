#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <math/math.h>
#include "asset_guid.h"

// CPU-side data of every asset type. Nothing here knows about the GPU: the renderer builds its own resources
// from these on its thread.
namespace wiwa
{
	constexpr u32 MAX_MESH_LODS = 4;

	// How the pixels of a texture are treated, which decides its GPU format and how its mips were made.
	enum class TextureKind : u32
	{
		Color,             // sRGB color data (albedo)
		Data,              // linear numbers (roughness, occlusion...)
		Normal,            // tangent-space normals
		MetallicRoughness, // glTF metallic-roughness map
	};

	enum class AlphaMode : u32
	{
		Opaque,
		Mask,
		Blend,
	};

	struct TextureData
	{
		u32 Width = 0;
		u32 Height = 0;
		TextureKind Kind = TextureKind::Color;
		std::vector<std::vector<u8>> Mips{}; // RGBA8, [0] is full size, each next one half the size (min 1x1)
	};

	// An environment as the GPU wants it, cooked from a Radiance .hdr file: the sky as a cube with its roughness-prefiltered mips, and
	// the diffuse light as spherical harmonics. Nothing of the source image is kept.
	struct EnvironmentData
	{
		u32 FaceSize = 0;                     // of mip 0; mip k is max(1, FaceSize >> k)
		std::vector<std::vector<u16>> Mips{}; // per mip: 6 faces (+X -X +Y -Y +Z -Z) x size x size x RGBA half floats, face-major, row-major
		std::array<glm::vec4, 9> SH{};        // rgb in xyz, order and meaning as project_sh9
	};

	// A .cube color grading table.
	struct LutData
	{
		std::string Title{};
		u32 Size = 0;
		std::vector<float> Table{}; // Size^3 * 3, red fastest
	};

	enum class ShaderStage : u32
	{
		Vertex,
		Pixel,
		Compute,
	};

	// What the binary is: DXBC (shader model 5.0) for D3D11 and D3D12, SPIR-V for Vulkan.
	enum class ShaderTarget : u32
	{
		Dxbc,
		Spirv,
	};

	struct ShaderBinary
	{
		ShaderStage Stage = ShaderStage::Vertex;
		ShaderTarget Target = ShaderTarget::Dxbc;
		std::string Entry{}; // VSMain, PSMain or CSMain
		std::vector<u8> Code{};
	};

	// A shader file cooked once into every binary a renderer backend can take. A game compiles nothing: it picks the
	// binary of its backend. A file without entry points (an include) has no binaries.
	struct ShaderData
	{
		std::string Name{};
		std::vector<ShaderBinary> Binaries{};

		const ShaderBinary* find(ShaderStage stage, ShaderTarget target) const
		{
			for (const ShaderBinary& binary : Binaries)
			{
				if (binary.Stage == stage && binary.Target == target)
				{
					return &binary;
				}
			}
			return nullptr;
		}
	};

	struct MeshVertex
	{
		float Position[3]{};
		float Normal[3]{};
		float Uv[2]{};
	};

	struct MeshLod
	{
		u32 FirstIndex = 0;
		u32 IndexCount = 0;
		float Error = 0.0f;
	};

	struct MeshSkinVertex
	{
		u32 Joints[4]{};
		float Weights[4]{};
	};

	// A run of triangles that share one material. Indices are relative to BaseVertex.
	struct MeshPrimitive
	{
		u32 IndexCount = 0;
		u32 FirstIndex = 0;
		i32 BaseVertex = 0;
		AssetRef Material{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		u32 LodCount = 1;
		MeshLod Lods[MAX_MESH_LODS]{};
		u32 VertexCount = 0;
		bool Skinned = false;
		u32 MorphTargetCount = 0;
		u32 MorphOffset = 0; // into MeshData::MorphPositions / MorphNormals: target * VertexCount + vertex
		float MaxMorphDelta = 0.0f;
	};

	struct MeshData
	{
		std::string Name{};
		std::vector<MeshVertex> Vertices{};
		std::vector<u32> Indices{};
		std::vector<MeshPrimitive> Primitives{};
		std::vector<MeshSkinVertex> SkinVertices{}; // parallel to Vertices, or empty
		std::vector<glm::vec4> MorphPositions{};
		std::vector<glm::vec4> MorphNormals{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
	};

	// A parameter of a user surface shader (.wshader). Offset is in bytes inside the parameter block, packed like an HLSL
	// cbuffer: a member never straddles a 16-byte boundary. Min == Max means the parameter has no range.
	constexpr u32 SURFACE_BLOCK_MAX_SIZE = 256; // bytes of the parameter block of a surface shader
	constexpr u32 SURFACE_MAX_TEXTURES = 4;     // user textures per surface shader

	enum class SurfaceParamType : u32
	{
		Float,
		Float2,
		Float3,
		Float4,
		Int,
		Bool,
	};

	struct SurfaceParam
	{
		std::string Name{};
		SurfaceParamType Type = SurfaceParamType::Float;
		u32 Offset = 0;
		float Min = 0.0f;
		float Max = 0.0f;
		float Default[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		bool Color = false;
		std::string Label{};
	};

	struct MaterialData
	{
		std::string Name{};
		glm::vec4 BaseColorFactor{ 1.0f };
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
		float MetallicFactor = 0.0f;
		float RoughnessFactor = 0.8f;
		glm::vec3 EmissiveFactor{ 0.0f };
		float Clearcoat = 0.0f;
		float ClearcoatRoughness = 0.0f;
		float SheenIntensity = 0.0f;
		float SheenRoughness = 0.0f;
		float Transmission = 0.0f;
		AssetRef BaseColorTexture{};
		AssetRef NormalTexture{};
		AssetRef OrmTexture{};
		AssetRef EmissiveTexture{};
	};

	// One skin of a model: the joints (node indices of the Prefab) and the matrices that bring a vertex into each joint's space.
	struct SkeletonData
	{
		std::string Name{};
		std::vector<u32> Joints{};
		std::vector<glm::mat4> InverseBind{};
	};

	enum class AnimationPath : u32
	{
		Translation,
		Rotation,
		Scale,
		Weights,
	};

	enum class AnimationInterpolation : u32
	{
		Step,
		Linear,
		CubicSpline,
	};

	struct AnimationChannel
	{
		u32 Node = 0; // node index in the Prefab
		AnimationPath Path = AnimationPath::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		u32 Components = 0;
		std::vector<float> Times{};
		std::vector<float> Values{};
	};

	struct AnimationClipData
	{
		std::string Name{};
		float Duration = 0.0f;
		std::vector<AnimationChannel> Channels{};
	};

	struct PrefabNode
	{
		std::string Name{};
		i32 Parent = -1;
		u64 StableId = 0;    // never 0 in a cooked prefab; does not change when the model is imported again (see scene/prefab_node_id.h)
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
		std::vector<float> MorphWeights{};
		AssetRef Mesh{};     // invalid when the node holds no mesh
		AssetRef Skeleton{}; // invalid when the node is not skinned
	};

	// The hierarchy of an imported model; it points to the meshes, skeletons and clips by reference.
	struct PrefabData
	{
		std::string Name{};
		std::vector<PrefabNode> Nodes{};
		std::vector<AssetRef> Animations{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
	};

	// A Lua script as the runtime loads it: the source text, and the catalog path it came from (for error messages).
	struct ScriptData
	{
		std::string Source{};
		std::string SourcePath{};
	};
}
