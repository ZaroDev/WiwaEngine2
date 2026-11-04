#include <editor_common.h>
#include "physics_section.h"

#include <core/engine.h>
#include <physics/physics_module.h>

#include <asset_pipeline/physics_settings_file.h>

PhysicsSection::PhysicsSection(std::filesystem::path file)
	: m_File(std::move(file))
{
	if (!wiwa::load_physics_layers(m_File, m_Layers, m_Error))
	{
		WI_WARN("Physics", "{0}: {1}; the default layers are used", m_File.string(), m_Error);
	}
	apply();
}

bool PhysicsSection::load()
{
	wiwa::PhysicsLayers loaded;
	std::string error;
	if (!wiwa::load_physics_layers(m_File, loaded, error))
	{
		m_Error = error;
		return false;
	}
	m_Error.clear();
	m_Layers = loaded;
	apply();
	return true;
}

bool PhysicsSection::save()
{
	m_Error.clear();
	return wiwa::save_physics_layers(m_File, m_Layers, m_Error);
}

void PhysicsSection::apply()
{
	if (wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>())
	{
		physics->set_layers(m_Layers);
	}
}

bool PhysicsSection::set_collides(wiwa::u32 a, wiwa::u32 b, bool collides)
{
	m_Layers.set_collides(a, b, collides);
	apply();
	return save();
}

bool PhysicsSection::set_layer_name(wiwa::u32 layer, const std::string& name)
{
	if (layer >= wiwa::PHYSICS_LAYER_COUNT)
	{
		return false;
	}
	m_Layers.Names[layer] = name;
	apply();
	return save();
}
