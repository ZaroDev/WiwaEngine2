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
			m_Instances.clear();
			m_InstanceIndex.clear();
			m_FreeInstances.clear();
			m_InstanceCount = 0;
			m_Lights.clear();
			m_LightIndex.clear();
			m_FreeLights.clear();
			m_LightCount = 0;
			m_Camera.reset();
		}

		const Slots<RenderInstance> instances{ &m_Instances, &m_InstanceIndex, &m_FreeInstances, &m_InstanceCount };
		const Slots<RenderLight> lights{ &m_Lights, &m_LightIndex, &m_FreeLights, &m_LightCount };

		for (const EntityGuid& guid : delta.InstanceRemovals)
		{
			remove(instances, guid);
		}
		for (auto& [guid, instance] : delta.InstanceUpserts)
		{
			upsert(instances, guid, std::move(instance));
		}
		for (const EntityGuid& guid : delta.LightRemovals)
		{
			remove(lights, guid);
		}
		for (auto& [guid, light] : delta.LightUpserts)
		{
			upsert(lights, guid, std::move(light));
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
}
