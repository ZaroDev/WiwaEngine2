#include "material_common.hlsli"

// The decal pass (decals.md): one box per decal, drawn from the vertex index, projecting the decal's material onto what the depth prepass
// drew. Four targets: premultiplied color, octahedral normal, occlusion / roughness / metallic and emissive light; the forward shaders read
// them (pbr.hlsli, ApplyDecals). The sets are frame (set 0), the decal's material (1) and this pass's own (2).

// One record per visible decal. Matrices are stored as four column vectors, as SceneInstance does. Mirrors DecalGpu in decal_pass.h.
struct DecalRecord
{
	float4 ModelC0;       // unit cube (-0.5..0.5) -> world
	float4 ModelC1;
	float4 ModelC2;
	float4 ModelC3;
	float4 InverseC0;     // world -> unit cube
	float4 InverseC1;
	float4 InverseC2;
	float4 InverseC3;
	float4 Axis;          // xyz = the way the decal projects (world), w = opacity
	float4 Fade;          // x = edge fade (fraction of the box), y = cos of the angle where it is gone, z = cos of the angle where it starts to fade
	uint4 Info;           // x = channels (1 color, 2 normal, 4 ORM, 8 emissive)
};

static const uint CHANNEL_COLOR = 1u;
static const uint CHANNEL_NORMAL = 2u;
static const uint CHANNEL_ORM = 4u;
static const uint CHANNEL_EMISSIVE = 8u;

VK_BINDING(28, 2) Texture2D<float> uDecalDepth : register(t28);
VK_BINDING(29, 2) STENCIL_TEXTURE uDecalStencil : register(t29);
VK_BINDING(30, 2) Texture2D<float2> uPrepassNormal : register(t30);
VK_BINDING(31, 2) StructuredBuffer<DecalRecord> uDecals : register(t31);

// Bit 4 of the stencil marks the pixels of meshes that do not receive decals (the depth prepass writes it).
static const uint STENCIL_NO_DECALS = 4u;

// The 12 triangles of a unit cube, counter-clockwise seen from outside. Corner bits: x = 1, y = 2, z = 4 (set = +0.5).
static const uint CUBE_INDICES[36] =
{
	1, 3, 7, 1, 7, 5, // +X
	0, 4, 6, 0, 6, 2, // -X
	2, 6, 7, 2, 7, 3, // +Y
	0, 1, 5, 0, 5, 4, // -Y
	4, 5, 7, 4, 7, 6, // +Z
	0, 2, 3, 0, 3, 1, // -Z
};

float3 TransformByColumns(float4 c0, float4 c1, float4 c2, float4 c3, float3 p)
{
	return c0.xyz * p.x + c1.xyz * p.y + c2.xyz * p.z + c3.xyz;
}

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
	DecalRecord decal = uDecals[DrawConstants.InstanceIndex];
	uint corner = CUBE_INDICES[vertexId];
	float3 local = float3((corner & 1u) != 0u ? 0.5 : -0.5, (corner & 2u) != 0u ? 0.5 : -0.5, (corner & 4u) != 0u ? 0.5 : -0.5);
	float3 world = TransformByColumns(decal.ModelC0, decal.ModelC1, decal.ModelC2, decal.ModelC3, local);
	return mul(ViewProjection, float4(world, 1.0));
}

struct DecalOutput
{
	float4 Albedo : SV_Target0;
	float4 Normal : SV_Target1;
	float4 Orm : SV_Target2;
	float4 Emissive : SV_Target3;
};

// Reoriented normal mapping: the decal's tangent-space normal laid over the surface's, both in the decal's frame (x, y in the decal's plane,
// z towards the projector).
float3 ReorientNormal(float3 surfaceNormal, float3 detail)
{
	float3 t = surfaceNormal + float3(0.0, 0.0, 1.0);
	t.z = max(t.z, 1e-3);
	float3 u = detail * float3(-1.0, -1.0, 1.0);
	return normalize(t * dot(t, u) / t.z - u);
}

DecalOutput PSMain(float4 position : SV_Position)
{
	DecalRecord decal = uDecals[DrawConstants.InstanceIndex];
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];

	int2 pixel = int2(position.xy);
	float depth = uDecalDepth.Load(int3(pixel, 0));

	// The point of the surface in this pixel, from the depth the prepass wrote under the jittered projection (InverseViewProjection is its inverse).
	float2 uv = position.xy * ViewportSize.zw;
	float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	float4 homogeneous = mul(InverseViewProjection, clip);
	float3 worldPosition = homogeneous.xyz / homogeneous.w;
	float3 local = TransformByColumns(decal.InverseC0, decal.InverseC1, decal.InverseC2, decal.InverseC3, worldPosition);

	// The uv in the box (the image's top is +y), and its derivatives for sampling, taken before anything is discarded.
	float2 decalUv = float2(local.x + 0.5, 0.5 - local.y);
	float2 duvdx = ddx(decalUv);
	float2 duvdy = ddy(decalUv);

	if (depth >= 1.0 || any(abs(local) > 0.5) || (STENCIL_LOAD(uDecalStencil, pixel) & STENCIL_NO_DECALS) != 0u)
	{
		discard;
	}

	// How much of the decal is here: its opacity, the fade at the box's sides, and the fade as the surface turns away from the projector.
	float2 sideDistance = 0.5 - abs(local.xy);
	float edge = decal.Fade.x > 0.0 ? saturate(min(sideDistance.x, sideDistance.y) / decal.Fade.x) : 1.0;
	float3 surfaceNormal = OctahedralDecode(uPrepassNormal.Load(int3(pixel, 0)));
	float facing = dot(surfaceNormal, -decal.Axis.xyz);
	float angle = saturate((facing - decal.Fade.y) / max(decal.Fade.z - decal.Fade.y, 1e-4));
	float weight = decal.Axis.w * edge * angle;

	float4 baseColor = material.BaseColor * uBaseColor.SampleGrad(uMaterialSampler, decalUv, duvdx, duvdy);
	float alpha = baseColor.a * weight;

	DecalOutput output;
	output.Albedo = (decal.Info.x & CHANNEL_COLOR) != 0u ? float4(baseColor.rgb * alpha, alpha) : float4(0.0, 0.0, 0.0, 0.0);

	output.Normal = float4(0.0, 0.0, 0.0, 0.0);
	if ((decal.Info.x & CHANNEL_NORMAL) != 0u && (material.TextureMask & MATERIAL_HAS_NORMAL) != 0u)
	{
		float3 tangentNormal = uNormal.SampleGrad(uMaterialSampler, decalUv, duvdx, duvdy).xyz * 2.0 - 1.0;
		// The decal's frame: its plane's axes in the world (x right, y up in the image) and z towards the projector.
		float3 axisZ = -decal.Axis.xyz;
		float3 axisX = normalize(decal.ModelC0.xyz);
		float3 axisY = cross(axisZ, axisX);
		float3 surfaceInFrame = float3(dot(surfaceNormal, axisX), dot(surfaceNormal, axisY), dot(surfaceNormal, axisZ));
		float3 result = ReorientNormal(surfaceInFrame, normalize(tangentNormal));
		float3 worldNormal = normalize(axisX * result.x + axisY * result.y + axisZ * result.z);
		output.Normal = float4((OctahedralEncode(worldNormal) * 0.5 + 0.5) * alpha, 0.0, alpha);
	}

	output.Orm = float4(0.0, 0.0, 0.0, 0.0);
	if ((decal.Info.x & CHANNEL_ORM) != 0u)
	{
		float occlusion = 1.0;
		float roughness = material.Roughness;
		float metallic = material.Metallic;
		if ((material.TextureMask & MATERIAL_HAS_ORM) != 0u)
		{
			float3 orm = uOrm.SampleGrad(uMaterialSampler, decalUv, duvdx, duvdy).rgb;
			occlusion = orm.r;
			roughness *= orm.g;
			metallic *= orm.b;
		}
		output.Orm = float4(float3(occlusion, saturate(roughness), saturate(metallic)) * alpha, alpha);
	}

	output.Emissive = float4(0.0, 0.0, 0.0, 0.0);
	if ((decal.Info.x & CHANNEL_EMISSIVE) != 0u)
	{
		float3 emissive = material.Emissive;
		if ((material.TextureMask & MATERIAL_HAS_EMISSIVE) != 0u)
		{
			emissive *= uEmissive.SampleGrad(uMaterialSampler, decalUv, duvdx, duvdy).rgb;
		}
		output.Emissive = float4(emissive * alpha, 0.0);
	}
	return output;
}
