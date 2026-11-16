#include "Common.hlsli"

// One compare-and-swap step of a bitonic sort (vfx.md) over the entries particle_sort_fill.hlsl wrote: the farthest particle first. The step is
// (K, J): the C++ side dispatches K = 2, 4 ... Padded and J = K/2 ... 1, one dispatch each.
struct SortStepData
{
	uint Offset;
	uint K;
	uint J;
	uint Padded;
};
PUSH_CONSTANTS(SortStepData, Step, b1);

RWStructuredBuffer<uint2> uSort : register(u3);

[numthreads(WI_PARTICLE_GROUP_SIZE, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint i = id.x;
	if (i >= Step.Padded)
	{
		return;
	}
	const uint partner = i ^ Step.J;
	if (partner <= i)
	{
		return;
	}

	const uint2 a = uSort[Step.Offset + i];
	const uint2 b = uSort[Step.Offset + partner];
	const bool descending = (i & Step.K) == 0u;
	const bool swap = descending ? asfloat(a.x) < asfloat(b.x) : asfloat(a.x) > asfloat(b.x);
	if (swap)
	{
		uSort[Step.Offset + i] = b;
		uSort[Step.Offset + partner] = a;
	}
}
