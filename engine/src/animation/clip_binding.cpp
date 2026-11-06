#include <pch.h>
#include "clip_binding.h"

#include <unordered_map>

#include "animation_log.h"

namespace wiwa
{
	ClipBinding bind_clip(const AnimationClipData& clip, const SkeletonData& skeleton)
	{
		std::unordered_map<u32, i32> jointOfNode;
		for (size_t j = 0; j < skeleton.Joints.size(); j++)
		{
			jointOfNode.emplace(skeleton.Joints[j], static_cast<i32>(j));
		}

		ClipBinding binding;
		binding.JointOfChannel.assign(clip.Channels.size(), -1);
		u32 foreign = 0;
		for (size_t c = 0; c < clip.Channels.size(); c++)
		{
			const AnimationChannel& channel = clip.Channels[c];
			auto found = jointOfNode.find(channel.Node);
			if (found != jointOfNode.end() && channel.Path != AnimationPath::Weights)
			{
				binding.JointOfChannel[c] = found->second;
			}
			else
			{
				binding.Unbound++;
				if (found == jointOfNode.end())
				{
					foreign++;
				}
			}
		}

		if (foreign > 0)
		{
			WI_ANIM_LOG(Warning, "The clip {0} has {1} channel(s) for nodes outside the skeleton {2}; they are ignored", clip.Name, foreign, skeleton.Name);
		}
		return binding;
	}
}
