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

// What the built-in material makes of a pixel before lighting: factors times textures, with the fallbacks standing in for
// missing ones. The surface shader contract (surface_contract.hlsli) starts from the same values.
struct BuiltinSurface
{
	float3 Albedo;
	float3 NormalTS;  // tangent space, (0, 0, 1) without a normal map
	float Metallic;
	float Roughness;
	float3 Emissive;
	float Alpha;
	float Occlusion;
};

BuiltinSurface EvaluateBuiltinSurface(MaterialRecord material, float2 uv)
{
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

	float3 tangentNormal = float3(0.0, 0.0, 1.0);
	if ((material.TextureMask & MATERIAL_HAS_NORMAL) != 0u)
	{
		tangentNormal = uNormal.Sample(uMaterialSampler, uv).xyz * 2.0 - 1.0;
	}

	float3 emissive = material.Emissive;
	if ((material.TextureMask & MATERIAL_HAS_EMISSIVE) != 0u)
	{
		emissive *= uEmissive.Sample(uMaterialSampler, uv).rgb;
	}

	BuiltinSurface surface;
	surface.Albedo = baseColor.rgb;
	surface.NormalTS = tangentNormal;
	surface.Metallic = saturate(metallic);
	surface.Roughness = clamp(roughness, 0.045, 1.0);
	surface.Emissive = emissive;
	surface.Alpha = baseColor.a;
	surface.Occlusion = occlusion;
	return surface;
}

// No tangents in the meshes: the tangent frame comes from the screen-space derivatives of position and uv (pixel shaders only).
// normal is the unit normal facing the viewer; tangent and bitangent are scaled together so the longer one has unit length.
void SurfaceTangentFrame(float3 worldPosition, float3 normal, float2 uv, out float3 tangent, out float3 bitangent)
{
	float3 dp1 = ddx(worldPosition);
	float3 dp2 = ddy(worldPosition);
	float2 duv1 = ddx(uv);
	float2 duv2 = ddy(uv);
	float3 dp2perp = cross(dp2, normal);
	float3 dp1perp = cross(normal, dp1);
	tangent = dp2perp * duv1.x + dp1perp * duv2.x;
	bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
	float invMax = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-12));
	tangent *= invMax;
	bitangent *= invMax;
}

// The decals the decal pass drew for this view (decal.hlsl), read where the forward shaders build a surface: set 2 on Vulkan, next to the AO.
// Four screen-sized targets, premultiplied by the coverage of the decals (alpha): albedo (linear), the octahedral world normal and the
// occlusion / roughness / metallic of the decals, and the light they add. Without decals every one is a 1x1 texel of zeros, read with the
// coordinates clamped, which changes nothing: albedo * (1 - 0) + 0, and the branches below are not taken.
VK_BINDING(25, 2) Texture2D<float4> uDecalAlbedo : register(t25);
VK_BINDING(26, 2) Texture2D<float4> uDecalNormal : register(t26);
VK_BINDING(27, 2) Texture2D<float4> uDecalOrm : register(t27);
VK_BINDING(28, 2) Texture2D<float4> uDecalEmissive : register(t28);

// pixel = SV_Position.xy. The normal is the shading normal in world space.
void ApplyDecals(float2 pixel, inout float3 albedo, inout float3 normal, inout float metallic, inout float roughness, inout float occlusion, inout float3 emissive)
{
	uint2 size;
	uDecalAlbedo.GetDimensions(size.x, size.y);
	const int3 texel = int3(min(uint2(pixel), size - 1u), 0);

	const float4 color = uDecalAlbedo.Load(texel);
	albedo = albedo * (1.0 - color.a) + color.rgb;

	const float4 decalNormal = uDecalNormal.Load(texel);
	if (decalNormal.a > 0.0)
	{
		const float3 decalDirection = OctahedralDecode(decalNormal.rg / decalNormal.a * 2.0 - 1.0);
		normal = normalize(lerp(normal, decalDirection, decalNormal.a));
	}

	const float4 decalOrm = uDecalOrm.Load(texel);
	if (decalOrm.a > 0.0)
	{
		const float3 orm = decalOrm.rgb / decalOrm.a;
		occlusion = lerp(occlusion, orm.r, decalOrm.a);
		roughness = lerp(roughness, orm.g, decalOrm.a);
		metallic = lerp(metallic, orm.b, decalOrm.a);
	}

	emissive += uDecalEmissive.Load(texel).rgb;
}

// What the lighting below takes, from the surface's values (roughness and metallic are brought into their valid range here).
SurfaceData MakeSurfaceData(float3 worldPosition, float3 normal, float3 albedo, float metallic, float roughness, float occlusion, float3 emissive, float alpha)
{
	metallic = saturate(metallic);
	roughness = clamp(roughness, 0.045, 1.0);

	SurfaceData surface;
	surface.Position = worldPosition;
	surface.Normal = normal;
	surface.View = normalize(CameraPosition.xyz - worldPosition);
	surface.Diffuse = albedo * (1.0 - metallic);
	surface.F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	surface.Roughness = roughness;
	surface.Occlusion = occlusion;
	surface.Emissive = emissive;
	surface.Alpha = alpha;
	return surface;
}

// Reads the built-in material at a pixel (pixel = SV_Position.xy, where the decals are looked up; blended materials get none).
SurfaceData LoadSurface(float3 worldPosition, float3 worldNormal, float2 uv, bool frontFace, float2 pixel)
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	BuiltinSurface builtin = EvaluateBuiltinSurface(material, uv);

	float3 normal = normalize(worldNormal);
	if (!frontFace)
	{
		normal = -normal;
	}
	if ((material.TextureMask & MATERIAL_HAS_NORMAL) != 0u)
	{
		float3 tangent;
		float3 bitangent;
		SurfaceTangentFrame(worldPosition, normal, uv, tangent, bitangent);
		normal = normalize(mul(builtin.NormalTS, float3x3(tangent, bitangent, normal)));
	}

	float3 albedo = builtin.Albedo;
	float metallic = builtin.Metallic;
	float roughness = builtin.Roughness;
	float occlusion = builtin.Occlusion;
	float3 emissive = builtin.Emissive;
	if ((material.Flags & MATERIAL_ALPHA_BLEND_BIT) == 0u)
	{
		ApplyDecals(pixel, albedo, normal, metallic, roughness, occlusion, emissive);
	}
	return MakeSurfaceData(worldPosition, normal, albedo, metallic, roughness, occlusion, emissive, builtin.Alpha);
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

// The blurred screen-space ambient occlusion, 1 = open (a white 1x1 texture when it is off). A layout of its own: set 2 on Vulkan.
VK_BINDING(24, 2) Texture2D<float> uAmbientOcclusion : register(t24);

// The forward pass's light at a pixel (pixel = SV_Position.xy): ambient dimmed by the AO, emission, the sun with its shadow and
// the clustered lights.
float3 ForwardLighting(SurfaceData surface, float2 pixel)
{
	// The ambient light is dimmed where the surroundings block it; the 1x1 stand-in of "no AO" is white wherever it is read.
	float ambientOcclusion = uAmbientOcclusion.SampleLevel(uEnvironmentSampler, pixel * ViewportSize.zw, 0);
	float3 color = AmbientLight(surface) * ambientOcclusion + surface.Emissive;
	if (SunDirection.w > 0.5)
	{
		float3 toTheSun = -SunDirection.xyz;
		float shadow = SunShadow(surface.Position, surface.Normal, saturate(dot(surface.Normal, toTheSun)));
		color += DirectLight(surface, toTheSun, SunColor.rgb) * shadow;
	}
	color += ClusteredLights(surface, pixel);
	return color;
}

#endif // PBR_HLSLI
