#include "scene_common.hlsli"

// Picking: draws every scene instance with its id (instance slot + 1) into an R32_UINT target. Same frame set and mesh layout as the
// depth prepass.

struct PickVertexOutput
{
	float4 Position : SV_Position;
	nointerpolation uint Id : COLOR0;
};

PickVertexOutput VSMain(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	PickVertexOutput output;
	output.Position = mul(UnjitteredViewProjection, float4(TransformPoint(instance, input.Position), 1.0));
	output.Id = DrawConstants.InstanceIndex + 1u;
	return output;
}

uint PSMain(PickVertexOutput input) : SV_Target0
{
	return input.Id;
}
