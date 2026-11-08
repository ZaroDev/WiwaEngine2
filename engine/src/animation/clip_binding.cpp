#include <pch.h>
#include "clip_binding.h"

#include <unordered_map>
#include <unordered_set>

#include "animation_log.h"

namespace wiwa
{
	ClipBinding bind_clip(const AnimationClipData& clip, const SkeletonData& skeleton)
	{
		std::unordered_map<std::string, i32> jointOfName;
		for (size_t j = 0; j < skeleton.JointNames.size(); j++)
		{
			jointOfName.emplace(skeleton.JointNames[j], static_cast<i32>(j));
		}

		ClipBinding binding;
		binding.JointOfChannel.assign(clip.Channels.size(), -1);
		u32 foreign = 0;
		for (size_t c = 0; c < clip.Channels.size(); c++)
		{
			const AnimationChannel& channel = clip.Channels[c];
			auto found = jointOfName.find(channel.Joint);
			if (found != jointOfName.end() && channel.Path != AnimationPath::Weights)
			{
				binding.JointOfChannel[c] = found->second;
			}
			else
			{
				binding.Unbound++;
				if (found == jointOfName.end() && channel.Path != AnimationPath::Weights)
				{
					foreign++;
				}
			}
		}

		if (foreign > 0)
		{
			WI_ANIM_LOG(Warning, "The clip {0} has {1} channel(s) for joints the skeleton {2} does not have; they are ignored", clip.Name, foreign, skeleton.Name);
		}
		return binding;
	}

	ClipCompatibilityReport clip_compatibility(const AnimationClipData& clip, const SkeletonData& skeleton)
	{
		std::unordered_set<std::string> joints(skeleton.JointNames.begin(), skeleton.JointNames.end());
		std::unordered_set<std::string> missing;
		ClipCompatibilityReport report;
		for (const AnimationChannel& channel : clip.Channels)
		{
			if (channel.Path == AnimationPath::Weights)
			{
				continue;
			}
			report.Channels++;
			if (joints.contains(channel.Joint))
			{
				report.Matched++;
			}
			else if (missing.insert(channel.Joint).second)
			{
				report.Missing.push_back(channel.Joint);
			}
		}
		if (report.Matched == report.Channels)
		{
			report.Result = ClipCompatibility::Full;
		}
		else
		{
			report.Result = report.Matched == 0 ? ClipCompatibility::None : ClipCompatibility::Partial;
		}
		return report;
	}
}
