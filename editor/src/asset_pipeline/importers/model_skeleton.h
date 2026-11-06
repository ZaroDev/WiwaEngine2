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

	// Adds the events listed in the "animationEvents" array of the model import settings ({clip, time, name}) to the clips of that
	// name and sorts them by time. An event for a clip the model does not have is warned about.
	void merge_animation_events(ImportedModel& model, const nlohmann::json& settings);
}
