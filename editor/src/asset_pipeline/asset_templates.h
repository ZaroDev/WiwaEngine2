#pragma once

#include <filesystem>
#include <string>

// The assets the Asset Browser can make from nothing: plain text on disk (or a folder), with no intermediate binary file.
namespace wiwa
{
	enum class CreateKind
	{
		Folder,
		Material,      // .wmat
		Script,        // .lua
		Scene,         // .wscene
		SurfaceShader, // .wshader
		HlslShader,    // .hlsl
		HlslInclude,   // .hlsli
		UiPanel,       // .wuipanel
		PhysicsMaterial, // .wphysmat
		InputMap,        // .winput
		AnimationGraph,  // .wgraph
		ParticleEffect,  // .wfx
		BehaviorTree,    // .wbt
	};

	// What the Create menu calls it, the name it starts with, and the extension the file gets ("" for a folder).
	const char* display_name(CreateKind kind);
	const char* default_name(CreateKind kind);
	const char* extension(CreateKind kind);

	// Empty if name can be used for a new asset of this kind in directory, else the reason. The extension is not part of the name.
	// Refused: nothing, characters Windows does not allow, a trailing dot or space, a device name (CON, NUL, COM1...), a very long
	// name, and a name that is taken by a file or folder with the same extension.
	std::string validate_name(const std::string& name, const std::filesystem::path& directory, CreateKind kind);

	// A file name made from any text (an entity's name): unusable characters become '_', trailing dots and spaces go, it is cut to the length
	// validate_name allows, a device name gets a '_' in front, and nothing left is "Prefab". Does not look at the disk.
	std::string sanitize_file_stem(const std::string& name);

	// Writes the folder or file, from a template for the kind, and returns its path. Empty (and nothing is overwritten) if the name is
	// refused by validate_name or the write fails.
	std::filesystem::path create_asset(CreateKind kind, const std::filesystem::path& directory, const std::string& name);
}
