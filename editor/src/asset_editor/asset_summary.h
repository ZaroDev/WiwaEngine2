#pragma once

#include <filesystem>
#include <string>

namespace wiwa
{
	class AssetDatabase;

	// A line about an animation file for a tooltip: "32 joints, 2 sockets", "1.20 s, 24 channels, 3 events", "1204 vertices, 2408 triangles, 32 joints".
	// Reads the file; empty for any other kind of file or one that cannot be read.
	std::string summarize_animation_file(const std::filesystem::path& file);

	// Where an extracted file came from: the name of the model whose import wrote it ("Wizard.fbx"), or empty when it is not in a <Model>_animation folder.
	std::string extracted_from(const std::filesystem::path& file);

	// "Re-extract from model": the model whose import wrote the file forgets it, and imports again, so that a fresh copy of the model's current data
	// is written next to it under a new name (nothing is overwritten, the old file stays). Empty on success, else why not.
	std::string reextract_from_model(AssetDatabase& database, const std::filesystem::path& file);
}
