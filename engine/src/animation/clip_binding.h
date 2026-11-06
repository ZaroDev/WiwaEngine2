#pragma once

#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	// Which joint of a skeleton each channel of a clip drives. Built once per (clip, skeleton).
	struct ClipBinding
	{
		std::vector<i32> JointOfChannel{}; // parallel to AnimationClipData::Channels, -1 for a channel of a node outside the skeleton
		u32 Unbound = 0;                   // channels with -1
	};

	// Logs one warning if channels are left unbound. Weights channels (morph targets) are unbound without a warning: they are not played.
	ClipBinding bind_clip(const AnimationClipData& clip, const SkeletonData& skeleton);
}
