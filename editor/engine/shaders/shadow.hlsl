#include "scene_common.hlsli"

// Draws shadow casters into a shadow map: positions only. The push constants' MaterialSlot carries the index of the shadow matrix
// (0..3 the sun's cascades, 4..7 the spot lights).
float4 VSMain(MeshVertexInput input) : SV_Position
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	return mul(ShadowMatrices[DrawConstants.MaterialSlot], float4(TransformPoint(instance, input.Position), 1.0));
}
