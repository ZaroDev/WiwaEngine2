#pragma once

#include <filesystem>

#include <assets/asset_types.h>

namespace wiwa
{
	// The Asset Editor tool a file kind opens in.
	enum class AssetEditorKind
	{
		None,
		Skeleton,
		Mesh,
		Clip,
		Graph,
		Effect,
		BehaviorTree,
		Vehicle
	};

	// One source file kind of the editor, found by its extension. The single table behind "which icon", "does it open in the Asset Editor",
	// "is it a model file", "does it get a thumbnail" and "does it have an animation summary": add a kind here, not in a call site.
	struct AssetKindInfo
	{
		const char* Extension;      // lower case, with the dot
		AssetType Type;             // the engine type it imports to (a model file is a Prefab)
		const char* Icon;           // an ICON_FK_ string
		AssetEditorKind Editor;     // None when no Asset Editor tool opens it
		bool IsModel;               // .glb .gltf .fbx: imports as a prefab
		bool AnimationFile;         // .wskel .wanim .wmesh: the native animation files (re-extract, Asset Editor entry in the browser menu)
		bool Summary;               // has a one-line summary (summarize_animation_file) for the browser tooltip
	};

	// The kind of the file (case-insensitive extension), or nullptr for an extension the table does not know.
	const AssetKindInfo* find_asset_kind(const std::filesystem::path& file);

	// The first kind of the table with this type that an Asset Editor tool opens, or nullptr.
	const AssetKindInfo* find_editable_asset_kind(AssetType type);

	// Whether an Asset Editor tool opens the file / the assets of this type.
	bool opens_in_asset_editor(const std::filesystem::path& file);
	bool opens_in_asset_editor(AssetType type);

	// A model file (.glb, .gltf, .fbx in any case).
	bool is_model_file(const std::filesystem::path& file);
	// A model file or a .wprefab: what a drop on the hierarchy or viewport instantiates.
	bool is_prefab_source(const std::filesystem::path& file);
	// .wskel, .wanim, .wmesh.
	bool is_animation_file(const std::filesystem::path& file);
	// The file has a one-line animation summary (.wskel .wanim .wmesh .wgraph).
	bool has_animation_summary(const std::filesystem::path& file);

	// The icon for the file, the generic file icon for an unknown extension.
	const char* asset_icon(const std::filesystem::path& file);
}
