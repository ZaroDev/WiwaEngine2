#pragma once

#include <filesystem>
#include <string>

#include <physics/physics_layers.h>

#include <string>

// The project's physics layer names and collision matrix (physics.wsettings). Every change is saved at once and given to the physics
// module, which applies it to the world that runs. The step settings (gravity, rate...) are the scene's and stay in the Physics panel.
class PhysicsSection final
{
public:
	// Reads the layers from `file` (the default layers if there is no file) and gives them to the physics module.
	explicit PhysicsSection(std::filesystem::path file);

	const char* name() const { return "Physics"; }
	bool load();
	bool save();
	void apply();
	const std::string& last_error() const { return m_Error; }

	const wiwa::PhysicsLayers& layers() const { return m_Layers; }
	// What a click in the matrix does: sets the pair, saves the file and updates the world. False (and why in last_error) if the save failed.
	bool set_collides(wiwa::u32 a, wiwa::u32 b, bool collides);
	bool set_layer_name(wiwa::u32 layer, const std::string& name);

private:
	std::filesystem::path m_File;
	wiwa::PhysicsLayers m_Layers{};
	std::string m_Error{};
};
