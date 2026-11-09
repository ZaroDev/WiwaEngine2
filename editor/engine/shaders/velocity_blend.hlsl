#define SKIN_SET 2 // the sets before it in this pass: see scene_common.hlsli
#include "depth.hlsli"
#include "material_common.hlsli"

// The velocity of blended surfaces: drawn after the forward pass over the velocity the prepass wrote, only where the surface is mostly
// opaque (alpha 0.5 or more), so that moving glass and particles reproject with their own motion instead of that of what is behind them.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

float2 PSMain(DepthVertexOutput input) : SV_Target0
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	float alpha = material.BaseColor.a * uBaseColor.Sample(uMaterialSampler, input.Uv).a;
	if (alpha < 0.5)
	{
		discard;
	}
	return DepthVelocity(input);
}
