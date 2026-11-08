#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asset_data.h"

// The source files of animation assets: a skeleton (.wskel, JSON), a skinned mesh (.wmesh, binary) and a clip (.wanim, binary). The importers
// cook them into the Skeleton, Mesh and AnimationClip assets, and the editors write the same files, so this is the one description of the
// formats. Every read is checked: a file that is not what it says gives std::nullopt and a reason, never a crash.
namespace wiwa
{
	constexpr u32 WANIM_MAGIC = 0x4D4E4157; // "WANM"
	constexpr u32 WANIM_VERSION = 1;
	constexpr u32 WMESH_MAGIC = 0x48534D57; // "WMSH"
	constexpr u32 WMESH_VERSION = 1;
	constexpr int WSKEL_VERSION = 2; // 2: sockets. Version 1 files (no sockets) are still read

	// .wskel: { "version", "name", "joints": [ { "name", "parent", "t", "r" (x y z w), "s", "pre" (16), "inverseBind" (16) } ], "sockets": [ { "name", "joint", "t", "r" (x y z w), "s" } ] }. Matrices are column major.
	// A socket name is not empty and unique, its joint names a joint, its rotation is a unit quaternion. A joint name is not empty and unique, a parent is -1 or the index of an earlier joint, and every number is finite.
	std::string write_skeleton_file(const SkeletonData& skeleton);
	std::optional<SkeletonData> read_skeleton_file(std::string_view text, std::string& error);

	// .wanim: magic, version, then the clip (AnimationClipData: channels by joint name, events, the skeleton it was made for).
	std::vector<std::byte> write_clip_file(const AnimationClipData& clip);
	std::optional<AnimationClipData> read_clip_file(std::span<const std::byte> bytes, std::string& error);

	// .wgraph: JSON { "version", "name", "skeleton" ({guid, local}), "parameters": [ { "name", "type" (float|int|bool|trigger), "default" } ],
	// "layers": [ { "name", "mode" (base|override|additive), "weight", "weightParameter", "mask": { "root", "falloff", "joints": [ { "joint", "weight" } ] },
	// "machine": { "default", "states": [ { "name", "body": { "clip" } | { "blend": { "x", "y", "samples": [ { "clip", "x", "y" } ] } }, "loop", "speed",
	// "speedParameter", "position" } ], "transitions": [ { "from" (absent: any state), "to", "duration", "exitTime", "conditions": [ { "parameter", "op", "value" } ] } ] } } ] }.
	// Ops: > < >= <= == != true false fired.
	constexpr int WGRAPH_VERSION = 1;
	std::string write_graph_file(const AnimationGraphData& graph);
	// validate false reads a graph that is still being made (a state without its clip): the editor opens those; the importer and Save use graph_valid.
	std::optional<AnimationGraphData> read_graph_file(std::string_view text, std::string& error, bool validate = true);
	// The rules of a graph that can be played (Save and the importer enforce them); false, and the first problem in error.
	bool graph_valid(const AnimationGraphData& graph, std::string& error);

	// .wmesh: magic, version, then the mesh (MeshData: geometry, primitives with their default materials, skin, the skeleton it skins to).
	std::vector<std::byte> write_mesh_file(const MeshData& mesh);
	std::optional<MeshData> read_mesh_file(std::span<const std::byte> bytes, std::string& error);
}
