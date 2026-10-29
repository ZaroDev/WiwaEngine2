#include "scene_common.hlsli"

// Sorts the point and spot lights into the cluster grid: one thread per cluster (screen tile x exponential depth slice) tests every
// light's sphere against the cluster's view-space box and writes the indices of those that reach it.
// Slots: b0 frame constants, t3 lights, u6 counts, u7 indices (unique across register types, see Common.hlsli).
RWStructuredBuffer<uint> uOutCounts : register(u6);
RWStructuredBuffer<uint> uOutIndices : register(u7);

float SquaredDistanceToBox(float3 point3, float3 boxMin, float3 boxMax)
{
	float3 nearest = clamp(point3, boxMin, boxMax);
	float3 delta = point3 - nearest;
	return dot(delta, delta);
}

[numthreads(4, 3, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= CLUSTERS_X || id.y >= CLUSTERS_Y || id.z >= CLUSTERS_Z)
	{
		return;
	}

	uint cluster = id.z * (CLUSTERS_X * CLUSTERS_Y) + id.y * CLUSTERS_X + id.x;

	// The slice's view-space depth range (positive distances in front of the camera).
	float nearPlane = CameraPosition.w;
	float farPlane = CameraParams.x;
	float ratio = farPlane / nearPlane;
	float sliceNear = nearPlane * pow(ratio, (float)id.z / CLUSTERS_Z);
	float sliceFar = nearPlane * pow(ratio, (float)(id.z + 1) / CLUSTERS_Z);

	// The tile's rectangle in normalized device coordinates (tile row 0 is the top of the image).
	float2 ndcMin = float2((float)id.x / CLUSTERS_X * 2.0 - 1.0, 1.0 - (float)(id.y + 1) / CLUSTERS_Y * 2.0);
	float2 ndcMax = float2((float)(id.x + 1) / CLUSTERS_X * 2.0 - 1.0, 1.0 - (float)id.y / CLUSTERS_Y * 2.0);

	// The box around the cluster: the four corners of the tile at both depths.
	float3 boxMin = float3(1e30, 1e30, 1e30);
	float3 boxMax = float3(-1e30, -1e30, -1e30);
	for (uint corner = 0; corner < 8; corner++)
	{
		float2 ndc = float2((corner & 1) ? ndcMax.x : ndcMin.x, (corner & 2) ? ndcMax.y : ndcMin.y);
		float depth = (corner & 4) ? sliceFar : sliceNear;
		float scale = ClusterParams.z > 0.5 ? 1.0 : depth; // a perspective view widens with depth, an orthographic one does not
		float3 viewPoint = float3(ndc.x * scale / Projection[0][0], ndc.y * scale / Projection[1][1], -depth);
		boxMin = min(boxMin, viewPoint);
		boxMax = max(boxMax, viewPoint);
	}

	uint count = 0;
	for (uint i = 0; i < LightInfo.x && count < MAX_LIGHTS_PER_CLUSTER; i++)
	{
		GpuLight light = uLights[i];
		float3 viewPosition = mul(View, float4(light.Position, 1.0)).xyz;
		if (SquaredDistanceToBox(viewPosition, boxMin, boxMax) <= light.Range * light.Range)
		{
			uOutIndices[cluster * MAX_LIGHTS_PER_CLUSTER + count] = i;
			count++;
		}
	}
	uOutCounts[cluster] = count;
}
