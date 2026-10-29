#ifndef PBR_HLSLI
#define PBR_HLSLI

#include "material_common.hlsli"

// Metallic-roughness shading shared by the forward passes. Light "intensity" is scaled by pi, so an intensity of 1 lights a white
// surface facing the light with a value of 1.

float DistributionGgx(float nDotH, float alpha)
{
	float a2 = alpha * alpha;
	float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

// Height-correlated Smith visibility (includes the 1 / (4 nDotL nDotV) of the specular term).
float VisibilitySmith(float nDotL, float nDotV, float alpha)
{
	float a2 = alpha * alpha;
	float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
	float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
	return 0.5 / max(gv + gl, 1e-5);
}

float3 FresnelSchlick(float3 f0, float vDotH)
{
	return f0 + (1.0 - f0) * pow(saturate(1.0 - vDotH), 5.0);
}

struct SurfaceData
{
	float3 Position;
	float3 Normal;
	float3 View;      // from the surface to the camera
	float3 Diffuse;   // base color of non-metals
	float3 F0;
	float Roughness;
	float Occlusion;
	float3 Emissive;
	float Alpha;
};

// Reads the material at a pixel: factors times textures, with the fallbacks standing in for missing ones.
SurfaceData LoadSurface(float3 worldPosition, float3 worldNormal, float2 uv, bool frontFace)
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];

	float4 baseColor = material.BaseColor * uBaseColor.Sample(uMaterialSampler, uv);
	float roughness = material.Roughness;
	float metallic = material.Metallic;
	float occlusion = 1.0;
	if ((material.TextureMask & MATERIAL_HAS_ORM) != 0u)
	{
		float3 orm = uOrm.Sample(uMaterialSampler, uv).rgb;
		occlusion = orm.r;
		roughness *= orm.g;
		metallic *= orm.b;
	}
	roughness = clamp(roughness, 0.045, 1.0);
	metallic = saturate(metallic);

	float3 normal = normalize(worldNormal);
	if (!frontFace)
	{
		normal = -normal;
	}
	if ((material.TextureMask & MATERIAL_HAS_NORMAL) != 0u)
	{
		// No tangents in the meshes: the tangent frame comes from the screen-space derivatives of position and uv.
		float3 tangentNormal = uNormal.Sample(uMaterialSampler, uv).xyz * 2.0 - 1.0;
		float3 dp1 = ddx(worldPosition);
		float3 dp2 = ddy(worldPosition);
		float2 duv1 = ddx(uv);
		float2 duv2 = ddy(uv);
		float3 dp2perp = cross(dp2, normal);
		float3 dp1perp = cross(normal, dp1);
		float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
		float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
		float invMax = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-12));
		normal = normalize(mul(tangentNormal, float3x3(tangent * invMax, bitangent * invMax, normal)));
	}

	float3 emissive = material.Emissive;
	if ((material.TextureMask & MATERIAL_HAS_EMISSIVE) != 0u)
	{
		emissive *= uEmissive.Sample(uMaterialSampler, uv).rgb;
	}

	SurfaceData surface;
	surface.Position = worldPosition;
	surface.Normal = normal;
	surface.View = normalize(CameraPosition.xyz - worldPosition);
	surface.Diffuse = baseColor.rgb * (1.0 - metallic);
	surface.F0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
	surface.Roughness = roughness;
	surface.Occlusion = occlusion;
	surface.Emissive = emissive;
	surface.Alpha = baseColor.a;
	return surface;
}

// The light from one direction (toTheLight is a unit vector from the surface to the light) with the given radiance.
float3 DirectLight(SurfaceData surface, float3 toTheLight, float3 radiance)
{
	float nDotL = saturate(dot(surface.Normal, toTheLight));
	if (nDotL <= 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}

	float3 halfVector = normalize(toTheLight + surface.View);
	float nDotV = max(dot(surface.Normal, surface.View), 1e-4);
	float nDotH = saturate(dot(surface.Normal, halfVector));
	float vDotH = saturate(dot(surface.View, halfVector));
	float alpha = surface.Roughness * surface.Roughness;

	float3 fresnel = FresnelSchlick(surface.F0, vDotH);
	float3 specular = DistributionGgx(nDotH, alpha) * VisibilitySmith(nDotL, nDotV, alpha) * fresnel;
	float3 diffuse = (1.0 - fresnel) * surface.Diffuse / PI;
	return (diffuse + specular) * radiance * (PI * nDotL);
}

float3 AmbientLight(SurfaceData surface)
{
	float3 hemisphere = lerp(GroundColor.rgb, SkyColor.rgb, surface.Normal.y * 0.5 + 0.5);
	return hemisphere * (surface.Diffuse + surface.F0 * (1.0 - surface.Roughness) * 0.5) * surface.Occlusion;
}

#endif // PBR_HLSLI
