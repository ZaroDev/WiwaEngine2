#include <pch.h>
#include "render_scene.h"

namespace wiwa
{
	template<typename T>
	void RenderScene::upsert(Slots<T> slots, EntityGuid guid, T&& value)
	{
		value.Alive = true;
		const auto found = slots.Index->find(guid);
		if (found != slots.Index->end())
		{
			(*slots.Items)[found->second] = std::move(value);
			return;
		}

		size_t slot = 0;
		if (!slots.Free->empty())
		{
			slot = slots.Free->back();
			slots.Free->pop_back();
			(*slots.Items)[slot] = std::move(value);
		}
		else
		{
			slot = slots.Items->size();
			slots.Items->push_back(std::move(value));
		}
		(*slots.Index)[guid] = slot;
		(*slots.Count)++;
	}

	template<typename T>
	void RenderScene::remove(Slots<T> slots, EntityGuid guid)
	{
		const auto found = slots.Index->find(guid);
		if (found == slots.Index->end())
		{
			return;
		}

		(*slots.Items)[found->second] = T{}; // Alive false, and the asset references let go
		slots.Free->push_back(found->second);
		slots.Index->erase(found);
		(*slots.Count)--;
	}

	void RenderScene::apply(SceneDelta&& delta)
	{
		if (delta.empty())
		{
			return;
		}

		if (delta.Reset)
		{
			m_ResetCount++;
			m_Instances.clear();
			m_InstanceIndex.clear();
			m_FreeInstances.clear();
			m_InstanceCount = 0;
			m_Lights.clear();
			m_LightIndex.clear();
			m_FreeLights.clear();
			m_LightCount = 0;
			m_Decals.clear();
			m_DecalIndex.clear();
			m_FreeDecals.clear();
			m_DecalCount = 0;
			m_Particles.clear();
			m_ParticleIndex.clear();
			m_FreeParticles.clear();
			m_ParticleCount = 0;
			m_Camera.reset();
		}

		const Slots<RenderInstance> instances{ &m_Instances, &m_InstanceIndex, &m_FreeInstances, &m_InstanceCount };
		const Slots<RenderLight> lights{ &m_Lights, &m_LightIndex, &m_FreeLights, &m_LightCount };
		const Slots<RenderDecal> decals{ &m_Decals, &m_DecalIndex, &m_FreeDecals, &m_DecalCount };
		const Slots<RenderParticleSystem> particles{ &m_Particles, &m_ParticleIndex, &m_FreeParticles, &m_ParticleCount };

		for (const EntityGuid& guid : delta.InstanceRemovals)
		{
			remove(instances, guid);
		}
		for (auto& [guid, instance] : delta.InstanceUpserts)
		{
			instance.Guid = guid;
			// A new instance has no motion; one that exists keeps the matrix of the last rendered frame, however many times it moves before the next.
			const auto existing = m_InstanceIndex.find(guid);
			instance.PrevWorld = existing != m_InstanceIndex.end() ? m_Instances[existing->second].PrevWorld : instance.World;
			if (existing != m_InstanceIndex.end())
			{
				instance.Palette = m_Instances[existing->second].Palette;
				instance.PrevPalette = m_Instances[existing->second].PrevPalette;
			}
			upsert(instances, guid, std::move(instance));
		}
		for (auto& [guid, rows] : delta.PaletteUpdates)
		{
			const auto found = m_InstanceIndex.find(guid);
			if (found == m_InstanceIndex.end())
			{
				continue;
			}
			RenderInstance& instance = m_Instances[found->second];
			// the first palette has no motion; a palette of another size cannot be compared with the one before it
			if (rows.empty() || instance.PrevPalette.size() != rows.size())
			{
				instance.PrevPalette = rows;
			}
			instance.Palette = std::move(rows);
		}
		for (const EntityGuid& guid : delta.LightRemovals)
		{
			remove(lights, guid);
		}
		for (auto& [guid, light] : delta.LightUpserts)
		{
			upsert(lights, guid, std::move(light));
		}
		for (const EntityGuid& guid : delta.DecalRemovals)
		{
			remove(decals, guid);
		}
		for (auto& [guid, decal] : delta.DecalUpserts)
		{
			upsert(decals, guid, std::move(decal));
		}
		for (const EntityGuid& guid : delta.ParticleRemovals)
		{
			remove(particles, guid);
		}
		for (auto& [guid, system] : delta.ParticleUpserts)
		{
			// the spawns still waiting for the simulation and the matrix of the last simulated frame belong to the render scene, not to the delta
			const auto existing = m_ParticleIndex.find(guid);
			system.PendingSpawns = existing != m_ParticleIndex.end() ? m_Particles[existing->second].PendingSpawns : std::array<u32, PARTICLE_MAX_EMITTERS>{};
			system.PrevWorld = existing != m_ParticleIndex.end() ? m_Particles[existing->second].PrevWorld : system.World;
			upsert(particles, guid, std::move(system));
		}
		for (const auto& [guid, spawns] : delta.ParticleSpawns)
		{
			const auto found = m_ParticleIndex.find(guid);
			if (found == m_ParticleIndex.end())
			{
				continue;
			}
			for (size_t i = 0; i < PARTICLE_MAX_EMITTERS; i++)
			{
				u32& pending = m_Particles[found->second].PendingSpawns[i];
				pending = std::min<u64>(static_cast<u64>(pending) + spawns[i], PARTICLE_LIMIT_MAX_PARTICLES);
			}
		}

		if (delta.Settings)
		{
			m_Settings = *delta.Settings;
		}

		if (delta.CameraRemoved)
		{
			m_Camera.reset();
		}
		if (delta.Camera)
		{
			m_Camera = delta.Camera;
		}

		m_Revision++;
	}

	void RenderScene::end_frame()
	{
		bool moved = false;
		for (RenderInstance& instance : m_Instances)
		{
			if (instance.Alive && instance.PrevWorld != instance.World)
			{
				instance.PrevWorld = instance.World;
				moved = true;
			}
			if (instance.Alive && !instance.Palette.empty() && instance.PrevPalette != instance.Palette)
			{
				instance.PrevPalette = instance.Palette;
				moved = true;
			}
		}
		m_PreviousStale = moved;
	}

	std::optional<size_t> RenderScene::find_instance(EntityGuid guid) const
	{
		const auto found = m_InstanceIndex.find(guid);
		return found == m_InstanceIndex.end() ? std::nullopt : std::optional<size_t>(found->second);
	}

	std::optional<size_t> RenderScene::find_light(EntityGuid guid) const
	{
		const auto found = m_LightIndex.find(guid);
		return found == m_LightIndex.end() ? std::nullopt : std::optional<size_t>(found->second);
	}

	std::optional<size_t> RenderScene::find_particle_system(EntityGuid guid) const
	{
		const auto found = m_ParticleIndex.find(guid);
		return found == m_ParticleIndex.end() ? std::nullopt : std::optional<size_t>(found->second);
	}

	std::array<u32, PARTICLE_MAX_EMITTERS> RenderScene::take_pending_spawns(size_t slot)
	{
		std::array<u32, PARTICLE_MAX_EMITTERS> spawns{};
		if (slot < m_Particles.size() && m_Particles[slot].Alive)
		{
			RenderParticleSystem& system = m_Particles[slot];
			spawns = system.PendingSpawns;
			system.PendingSpawns.fill(0);
			system.PrevWorld = system.World;
		}
		return spawns;
	}

	std::optional<size_t> RenderScene::find_decal(EntityGuid guid) const
	{
		const auto found = m_DecalIndex.find(guid);
		return found == m_DecalIndex.end() ? std::nullopt : std::optional<size_t>(found->second);
	}
}
