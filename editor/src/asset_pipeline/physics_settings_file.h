#pragma once

#include <filesystem>
#include <string>

#include <physics/physics_layers.h>

// The project's physics settings (physics.wsettings, in the editor's working directory next to build_settings.json): JSON with "layers"
// (up to 32 objects { "name": ... }, the first is layer 0) and "matrix" (32 integers, bit b of entry a: layer a collides with layer b). The
// packager puts them into assets.wpak (physics/physics_project_settings.h), the Physics panel edits them.
namespace wiwa
{
	constexpr const char* PHYSICS_SETTINGS_FILE = "physics.wsettings";

	// The layers of a text; false with a reason when it is not JSON or a value is of the wrong type. Keys that are missing keep the default
	// (everything collides), a matrix is made symmetric (a pair collides only when both entries say so).
	bool physics_layers_from_text(const std::string& text, PhysicsLayers& out, std::string& error);
	std::string physics_layers_to_text(const PhysicsLayers& layers);

	// A file that does not exist gives the default layers and true; a file that cannot be read or parsed gives false and the default.
	bool load_physics_layers(const std::filesystem::path& path, PhysicsLayers& out, std::string& error);
	bool save_physics_layers(const std::filesystem::path& path, const PhysicsLayers& layers, std::string& error);
}
