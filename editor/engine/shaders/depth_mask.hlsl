#include "depth.hlsli"
#include "material_common.hlsli"

// The depth prepass for alpha-masked materials: pixels under the cutoff write no depth and no normal.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

float2 PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	float alpha = material.BaseColor.a * uBaseColor.Sample(uMaterialSampler, input.Uv).a;
	if (alpha < material.AlphaCutoff)
	{
		discard;
	}
	return DepthNormal(input, frontFace);
}
