#include "depth.hlsli"
#include "material_common.hlsli"

// The depth prepass for alpha-masked materials: pixels under the cutoff write no depth, normal or velocity.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

DepthOutput PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace)
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	float alpha = material.BaseColor.a * uBaseColor.Sample(uMaterialSampler, input.Uv).a;
	if (alpha < material.AlphaCutoff)
	{
		discard;
	}
	return DepthPixel(input, frontFace);
}
