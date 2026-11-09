#pragma once

#include <nlohmann/json.hpp>

#include "model_import.h"

namespace wiwa
{
	constexpr size_t SKELETON_MAX_JOINTS = 256; // the matrix palette of one skinned instance

	// Fills in what the readers do not: for every skeleton the joint names, the parent of each joint and the rest pose, then orders the
	// joints so that a parent comes before its children and gives the vertices of the meshes the skeleton skins the new joint indices.
	// The nodes between two joints, and above a root joint, are kept in SkeletonData::Pre (the pose is in the space of the
	// model root); the rest pose is the joint's own node. Warns (model.Warnings) above SKELETON_MAX_JOINTS.
	void finalize_model_skeletons(ImportedModel& model);

	// Gives every rigid (not skinned) mesh node that hangs below a joint of the skeleton an AnimationSocket on that joint, with the rest
	// pose between them as the offset, so a sword, a pad or a face follows the animation. A mesh below another such mesh follows its
	// parent. Call it with the prefab nodes after finalize_model_skeletons (the socket names the final joint names).
	void add_attachment_sockets(const ImportedModel& model, std::vector<PrefabNode>& nodes);

	// Adds the events listed in the "animationEvents" array of the model import settings ({clip, time, name}) to the clips of that
	// name and sorts them by time. An event for a clip the model does not have is warned about.
	void merge_animation_events(ImportedModel& model, const nlohmann::json& settings);

	// The clip with its channels turned from node indices into joint names, through the skeleton that has the most of the nodes the clip
	// animates (skeletonIndex, -1 when no skeleton has any). Channels for nodes that are no joint of that skeleton, and morph-weight channels,
	// are left out with one warning per clip. Call it after finalize_model_skeletons (it orders the joints and names them).
	AnimationClipData make_named_clip(const ImportedClip& clip, const std::vector<ImportedSkeleton>& skeletons, int& skeletonIndex, std::vector<std::string>& warnings);
}
