#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	class AssetDatabase;

	// A native animation file (.wmesh or .wanim) that names a skeleton.
	struct SkeletonDependent
	{
		std::filesystem::path File{};
		bool IsMesh = false;     // .wmesh (else .wanim)
		u32 Joints = 0;          // how many of the names it uses a rename changes
		bool Writable = true;
	};

	struct SkeletonDependents
	{
		std::vector<SkeletonDependent> Files{};
		std::vector<std::filesystem::path> Unreadable{}; // .wmesh/.wanim files that could not be read: they may or may not name the skeleton
	};

	// The meshes and clips that name one skeleton.
	struct SkeletonUsers
	{
		std::vector<std::filesystem::path> Meshes{};
		std::vector<std::filesystem::path> Clips{};
	};

	// One pass over every .wmesh and .wanim under the source roots: which skeleton (redirects followed) each one names. Paths are sorted. Stops early,
	// with what it has, when cancel is set.
	std::unordered_map<AssetGuid, SkeletonUsers> index_skeleton_users(const AssetDatabase& database, const std::atomic<bool>* cancel = nullptr);

	// Reads every .wmesh and .wanim under the source roots and lists those whose skeleton is this one (a reference to a legacy sub-asset is followed
	// through the redirects). Joints counts the names a rename changes; with no renames it is 0. Stops early, with what it has, when cancel is set.
	SkeletonDependents find_skeleton_dependents(const AssetDatabase& database, AssetGuid skeleton,
		const std::vector<std::pair<std::string, std::string>>& renames = {}, const std::atomic<bool>* cancel = nullptr);

	// Rewrites the joint names of the dependents that use a renamed name, then writes the skeleton file (the caller gives its bytes) last. The renames
	// apply all at once, so two joints can swap names. Every file is read again and its new content made before anything is written; if a write fails the
	// files already written are put back as they were and the skeleton is not written, so there is never a half-renamed set. False, and why in error.
	bool apply_joint_renames(const SkeletonDependents& dependents, const std::vector<std::pair<std::string, std::string>>& renames,
		const std::filesystem::path& skeletonFile, const std::string& skeletonText, std::string& error);
}
