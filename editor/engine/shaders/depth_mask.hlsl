#include "depth.hlsli"
#include "material_common.hlsli"

// The depth prepass for alpha-masked materials: pixels under the cutoff write no depth.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

void PSMain(DepthVertexOutput input)
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	float alpha = material.BaseColor.a * uBaseColor.Sample(uMaterialSampler, input.Uv).a;
	if (alpha < material.AlphaCutoff)
	{
		discard;
	}
}
