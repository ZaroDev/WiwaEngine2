#include "pbr.hlsli"

// The forward pass: PBR metallic-roughness for opaque and alpha-masked materials (the depth prepass has already cut the masked
// pixels out), and the same shading for blended ones.

// The blurred screen-space ambient occlusion, 1 = open (a white 1x1 texture when it is off). A layout of its own: set 2 on Vulkan.
VK_BINDING(15, 2) Texture2D<float> uAmbientOcclusion : register(t15);

struct ForwardVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD0;
	float3 WorldNormal : TEXCOORD1;
	float2 Uv : TEXCOORD2;
};

ForwardVertexOutput VSMain(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	float3 worldPosition = TransformPoint(instance, input.Position);

	ForwardVertexOutput output;
	output.Position = mul(ViewProjection, float4(worldPosition, 1.0));
	output.WorldPosition = worldPosition;
	output.WorldNormal = TransformNormal(instance, input.Normal);
	output.Uv = input.Uv;
	return output;
}

float4 PSMain(ForwardVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	SurfaceData surface = LoadSurface(input.WorldPosition, input.WorldNormal, input.Uv, frontFace);

	// The depth prepass left no depth where a masked material is cut out, so these pixels must not be shaded here either.
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	if ((material.Flags & MATERIAL_ALPHA_MASK_BIT) != 0u && surface.Alpha < material.AlphaCutoff)
	{
		discard;
	}

	// The ambient light is dimmed where the surroundings block it; the 1x1 stand-in of "no AO" is white wherever it is read.
	float ambientOcclusion = uAmbientOcclusion.SampleLevel(uEnvironmentSampler, input.Position.xy * ViewportSize.zw, 0);
	float3 color = AmbientLight(surface) * ambientOcclusion + surface.Emissive;
	if (SunDirection.w > 0.5)
	{
		float3 toTheSun = -SunDirection.xyz;
		float shadow = SunShadow(surface.Position, surface.Normal, saturate(dot(surface.Normal, toTheSun)));
		color += DirectLight(surface, toTheSun, SunColor.rgb) * shadow;
	}
	color += ClusteredLights(surface, input.Position.xy);
	return float4(color, surface.Alpha);
}
