#include "Common.hlsli"

// Automatic exposure, step 2: one group. Averages the middle of the histogram, turns it into the exposure that shows
// that average at the target brightness, moves the stored exposure towards it, and clears the histogram for the next
// frame. Mirrors average_bin / bin_to_luminance / target_exposure / adapt_exposure in ExposureMath.h.

#define BINS WI_EXPOSURE_BINS

// Slots match ExposurePass: UAV 0 and 1, push constants 2.
RWStructuredBuffer<uint> uHistogram : register(u0);
RWStructuredBuffer<float> uExposure : register(u1); // [0] = the current exposure, read by the tonemap pass

struct ResolveData
{
	float MinLog2;
	float RangeLog2;
	float LowShare;
	float HighShare;
	float Key;
	float CompensationEv;
	float MinEv;
	float MaxEv;
	float DeltaSeconds;
	float Speed;
	float Reset; // 1 = no history: jump to the target
	float Padding;
};

PUSH_CONSTANTS(ResolveData, Constants, b2);

groupshared uint gs_bins[BINS];

[numthreads(BINS, 1, 1)]
void CSMain(uint group_index : SV_GroupIndex)
{
	gs_bins[group_index] = uHistogram[group_index];
	GroupMemoryBarrierWithGroupSync();

	if (group_index == 0)
	{
		float total = 0.0;
		for (uint i = 1; i < BINS; i++)
		{
			total += float(gs_bins[i]);
		}

		float low_count = total * saturate(Constants.LowShare);
		float high_count = total * saturate(Constants.HighShare);

		float seen = 0.0;
		float sum = 0.0;
		float weight = 0.0;
		for (uint j = 1; j < BINS; j++)
		{
			float count = float(gs_bins[j]);
			float part = max(min(seen + count, high_count) - max(seen, low_count), 0.0);
			sum += part * float(j);
			weight += part;
			seen += count;
		}

		// Nothing measurable (a black frame): keep the exposure as it is.
		if (weight > 0.0)
		{
			float average_bin = sum / weight;
			float average_luminance = exp2(Constants.MinLog2 + (average_bin - 1.0) / 254.0 * Constants.RangeLog2);
			float target = Constants.Key / max(average_luminance, 1e-4) * exp2(Constants.CompensationEv);
			target = clamp(target, exp2(Constants.MinEv), exp2(Constants.MaxEv));

			float current = uExposure[0];
			if (Constants.Reset > 0.5 || !(current > 0.0))
			{
				current = target;
			}
			else
			{
				current += (target - current) * (1.0 - exp(-max(Constants.DeltaSeconds, 0.0) * max(Constants.Speed, 0.0)));
			}
			uExposure[0] = current;
		}
	}

	// Everyone has read its bin: the histogram can be emptied for the next frame.
	GroupMemoryBarrierWithGroupSync();
	uHistogram[group_index] = 0;
}
