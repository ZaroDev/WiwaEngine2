#pragma once

#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	// Which joint of a skeleton each channel of a clip drives (by the channel's joint name). Built once per (clip, skeleton).
	struct ClipBinding
	{
		std::vector<i32> JointOfChannel{}; // parallel to AnimationClipData::Channels, -1 for a channel whose joint the skeleton does not have
		u32 Unbound = 0;                   // channels with -1
	};

	// Logs one warning if channels are left unbound. Weights channels (morph targets) are unbound without a warning: they are not played.
	ClipBinding bind_clip(const AnimationClipData& clip, const SkeletonData& skeleton);

	// Whether a skeleton has the joints a clip names: Full (every channel has its joint), Partial (some do not: the others play), None (no channel
	// has a joint: the clip is of no use on this skeleton). Weights channels are not counted; a clip without channels is Full.
	enum class ClipCompatibility { Full, Partial, None };

	struct ClipCompatibilityReport
	{
		ClipCompatibility Result = ClipCompatibility::None;
		u32 Channels = 0;                 // counted channels
		u32 Matched = 0;
		std::vector<std::string> Missing{}; // the joint names the skeleton lacks (each once)
	};

	ClipCompatibilityReport clip_compatibility(const AnimationClipData& clip, const SkeletonData& skeleton);
}
