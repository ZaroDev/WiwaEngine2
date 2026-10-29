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

// How much of the sun reaches a point (1 = all, 0 = none): the cascade is chosen by view depth, the receiver is moved along its normal,
// and 3x3 hardware-filtered comparisons soften the edge.
float SunShadow(float3 worldPosition, float3 normal, float nDotL)
{
	if (ShadowParams.x < 0.5)
	{
		return 1.0;
	}

	float viewDepth = -mul(View, float4(worldPosition, 1.0)).z;
	uint count = (uint)ShadowParams.y;
	if (viewDepth > CascadeSplits[count - 1])
	{
		return 1.0; // beyond the shadow distance
	}
	uint cascade = 0;
	for (uint c = 0; c + 1 < count; c++)
	{
		if (viewDepth > CascadeSplits[c])
		{
			cascade = c + 1;
		}
	}

	float3 receiver = worldPosition + normal * (CascadeTexels[cascade] * ShadowParams.w);
	float4 clip = mul(ShadowMatrices[cascade], float4(receiver, 1.0));
	float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
	if (any(uv < 0.0) || any(uv > 1.0) || clip.z > 1.0)
	{
		return 1.0;
	}

	float reference = clip.z - ShadowParams.z * (1.0 + 2.0 * (1.0 - nDotL));
	float lit = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			float2 offset = float2(x, y) / CASCADE_MAP_SIZE;
			lit += uShadowCascades.SampleCmpLevelZero(uShadowSampler, float3(uv + offset, cascade), reference);
		}
	}
	return lit / 9.0;
}

// The same for a spot light's tile of the atlas (perspective depth).
float SpotShadow(GpuLight light, float3 worldPosition, float3 normal)
{
	uint slot = light.ShadowIndex - 1;
	float distanceToLight = length(light.Position - worldPosition);
	float texelWorld = 2.0 * distanceToLight * SpotShadowInfo[slot].x / SPOT_TILE_SIZE;
	float3 receiver = worldPosition + normal * (texelWorld * ShadowParams.w);

	float4 clip = mul(ShadowMatrices[4 + slot], float4(receiver, 1.0));
	if (clip.w <= 0.0)
	{
		return 1.0;
	}
	clip.xyz /= clip.w;
	float2 tile = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
	if (any(tile < 0.0) || any(tile > 1.0) || clip.z > 1.0)
	{
		return 1.0;
	}

	float2 uv = SpotShadowRects[slot].xy + tile * SpotShadowRects[slot].zw;
	float reference = clip.z - ShadowParams2.x;
	float lit = 0.0;
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			lit += uSpotAtlas.SampleCmpLevelZero(uShadowSampler, uv + float2(x, y) / SPOT_ATLAS_SIZE, reference);
		}
	}
	return lit / 9.0;
}

// A point or spot light: inverse-square falloff with a smooth cut at its range, and the cone for a spot.
float3 LocalLight(SurfaceData surface, GpuLight light)
{
	float3 toLight = light.Position - surface.Position;
	float distance2 = dot(toLight, toLight);
	float range2 = light.Range * light.Range;
	if (distance2 >= range2)
	{
		return float3(0.0, 0.0, 0.0);
	}

	float3 direction = toLight * rsqrt(max(distance2, 1e-8));
	float falloff = saturate(1.0 - distance2 * distance2 / (range2 * range2));
	float attenuation = falloff * falloff / (distance2 + 0.01);
	if (light.Type == LIGHT_SPOT)
	{
		float angle = dot(-direction, light.Direction);
		float cone = saturate((angle - light.OuterCos) / max(light.InnerCos - light.OuterCos, 1e-4));
		attenuation *= cone * cone;
		if (light.ShadowIndex > 0)
		{
			attenuation *= SpotShadow(light, surface.Position, surface.Normal);
		}
	}
	return DirectLight(surface, direction, light.Color * (light.Intensity * attenuation));
}

// Every light of the pixel's cluster.
float3 ClusteredLights(SurfaceData surface, float2 pixel)
{
	float3 result = float3(0.0, 0.0, 0.0);
	if (LightInfo.x == 0)
	{
		return result;
	}

	float viewDepth = -mul(View, float4(surface.Position, 1.0)).z;
	if (ClusterParams.w > 0.5)
	{
		// Debug: every light, no clusters (for comparing against the clustered result).
		for (uint l = 0; l < LightInfo.x; l++)
		{
			result += LocalLight(surface, uLights[l]);
		}
		return result;
	}
	uint cluster = ClusterIndexOf(pixel, viewDepth);
	uint count = min(uClusterCounts[cluster], MAX_LIGHTS_PER_CLUSTER);
	for (uint i = 0; i < count; i++)
	{
		result += LocalLight(surface, uLights[uClusterIndices[cluster * MAX_LIGHTS_PER_CLUSTER + i]]);
	}
	return result;
}

// The diffuse light (E / pi) from the SH, with the constants and the order of project_sh9 in ibl_math.cpp.
float3 EvaluateSh(float3 n)
{
	float3 sum = EnvSH[0].rgb * 0.282095
		+ EnvSH[1].rgb * (0.488603 * n.y) + EnvSH[2].rgb * (0.488603 * n.z) + EnvSH[3].rgb * (0.488603 * n.x)
		+ EnvSH[4].rgb * (1.092548 * n.x * n.y) + EnvSH[5].rgb * (1.092548 * n.y * n.z)
		+ EnvSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0)) + EnvSH[7].rgb * (1.092548 * n.x * n.z)
		+ EnvSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
	return max(sum, 0.0);
}

float3 AmbientLight(SurfaceData surface)
{
	if (EnvParams.x > 0.5)
	{
		// Diffuse from the SH, specular from the prefiltered cube (the mip follows the roughness) and the BRDF table.
		float3 diffuse = EvaluateSh(RotateEnvironment(surface.Normal)) * surface.Diffuse;
		float3 specular = float3(0.0, 0.0, 0.0);
		if (EnvParams.w > 0.5)
		{
			float nDotV = saturate(dot(surface.Normal, surface.View));
			float2 scaleBias = uBrdfLut.SampleLevel(uEnvironmentSampler, float2(nDotV, surface.Roughness), 0);
			float3 reflection = RotateEnvironment(reflect(-surface.View, surface.Normal));
			float3 prefiltered = uEnvironment.SampleLevel(uEnvironmentSampler, reflection, surface.Roughness * EnvParams.y).rgb;
			specular = prefiltered * (surface.F0 * scaleBias.x + scaleBias.y);
		}
		return (diffuse + specular) * (EnvParams.z * surface.Occlusion);
	}

	float3 hemisphere = lerp(GroundColor.rgb, SkyColor.rgb, surface.Normal.y * 0.5 + 0.5);
	return hemisphere * (surface.Diffuse + surface.F0 * (1.0 - surface.Roughness) * 0.5) * surface.Occlusion;
}

#endif // PBR_HLSLI
