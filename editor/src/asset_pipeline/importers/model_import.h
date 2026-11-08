#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "assets/asset_data.h"

// What the glTF and FBX readers produce, before the model importer splits it into Mesh, Material, Texture, Skeleton,
// AnimationClip and Prefab assets. Nothing here is written to disk as is.
namespace wiwa
{
	struct ImportedImage
	{
		std::filesystem::path File{};       // next to the model; empty for an image embedded in the model file
		std::vector<std::byte> Embedded{};  // the bytes of an embedded PNG/JPEG
		std::string Name{};
		TextureKind Kind = TextureKind::Color;
		bool Used = false;                  // some material refers to it
	};

	struct ImportedMaterial
	{
		MaterialData Data{};                // texture references are filled in by the importer
		int BaseColorImage = -1;
		int NormalImage = -1;
		int OrmImage = -1;
		int EmissiveImage = -1;
		bool PackedOrm = false;             // the ORM image really holds occlusion in red
	};

	struct ImportedMesh
	{
		MeshData Data{};
		std::vector<u32> PrimitiveMaterials{}; // per primitive: index into ImportedModel::Materials
	};

	// A skeleton as the readers see it: Joints are the nodes of the model that are the joints, in the order of the arrays of SkeletonData.
	// The importer turns it into a skeleton of joint names (the nodes mean nothing outside the model).
	struct ImportedSkeleton : SkeletonData
	{
		std::vector<u32> Joints{};
	};

	// A clip as the readers see it: ChannelNodes[i] is the node channel i animates. Turned into a clip of joint names by make_named_clip.
	struct ImportedClip : AnimationClipData
	{
		std::vector<u32> ChannelNodes{};
	};

	struct ImportedModel
	{
		std::string Name{};
		std::vector<PrefabNode> Nodes{};       // Mesh and Skeleton references are filled in by the importer
		std::vector<int> NodeMesh{};           // per node: index into Meshes, -1 for none
		std::vector<int> NodeSkin{};           // per node: index into Skeletons, -1 for none
		std::vector<ImportedMesh> Meshes{};
		std::vector<ImportedMaterial> Materials{}; // the last one is the default, for primitives that name none
		std::vector<ImportedImage> Images{};
		std::vector<ImportedSkeleton> Skeletons{};
		std::vector<ImportedClip> Animations{};
		std::vector<std::string> Warnings{};
	};

	// Both return false with a reason in error when the file cannot be used.
	bool read_gltf_model(const std::filesystem::path& path, ImportedModel& model, std::string& error);
	bool read_fbx_model(const std::filesystem::path& path, ImportedModel& model, std::string& error);
}
