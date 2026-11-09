#include <pch.h>
#include "particle_pool.h"

#include <algorithm>

#define WI_PARTICLE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	void ParticlePool::set_capacity(u32 capacity)
	{
		shutdown();
		m_Capacity = std::max(capacity, 1u);
	}

	void ParticlePool::shutdown()
	{
		m_Generation++;
		m_Particles = nullptr;
		m_Emitters = nullptr;
		m_Tables = nullptr;
		m_Ranges = RangeAllocator{};
		m_Live.clear();
		std::fill(m_IdUsed.begin(), m_IdUsed.end(), false);
		Draws.clear();
	}

	bool ParticlePool::ensure(nvrhi::IDevice* device)
	{
		if (m_Particles != nullptr)
		{
			return true;
		}

		const auto make = [&](u64 elements, u32 stride, bool unorderedAccess, const char* name)
		{
			nvrhi::BufferDesc description;
			description.byteSize = elements * stride;
			description.structStride = stride;
			description.canHaveUAVs = unorderedAccess;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = name;
			return device->createBuffer(description);
		};
		m_Particles = make(m_Capacity, sizeof(ParticleGpu), true, "Particle pool");
		m_Emitters = make(MAX_EMITTERS, sizeof(EmitterGpu), false, "Particle emitters");
		m_Tables = make(static_cast<u64>(MAX_EMITTERS) * PARTICLE_TABLE_ROWS, sizeof(glm::vec4), false, "Particle tables");
		if (!m_Particles || !m_Emitters || !m_Tables)
		{
			WI_PARTICLE_LOG(Error, "Could not create the particle pool of {0} particles", m_Capacity);
			shutdown();
			return false;
		}

		m_Ranges = RangeAllocator(m_Capacity);
		return true;
	}

	std::optional<ParticleAllocation> ParticlePool::allocate(u32 count)
	{
		if (m_Particles == nullptr || count == 0)
		{
			return std::nullopt;
		}

		const auto free = std::find(m_IdUsed.begin(), m_IdUsed.end(), false);
		if (free == m_IdUsed.end())
		{
			return std::nullopt;
		}
		const std::optional<u64> base = m_Ranges.allocate(count);
		if (!base)
		{
			return std::nullopt;
		}

		ParticleAllocation allocation;
		allocation.Id = static_cast<u32>(free - m_IdUsed.begin());
		allocation.Base = static_cast<u32>(*base);
		allocation.Capacity = count;
		m_IdUsed[allocation.Id] = true;
		m_Live.push_back(allocation);
		return allocation;
	}

	void ParticlePool::release(const ParticleAllocation& allocation)
	{
		const auto found = std::find_if(m_Live.begin(), m_Live.end(), [&](const ParticleAllocation& live) { return live.Id == allocation.Id && live.Base == allocation.Base; });
		if (found == m_Live.end())
		{
			return;
		}
		m_Ranges.free(found->Base, found->Capacity);
		m_IdUsed[found->Id] = false;
		m_Live.erase(found);
	}

	u32 ParticlePool::slot_end() const
	{
		u32 end = 0;
		for (const ParticleAllocation& live : m_Live)
		{
			end = std::max(end, live.Base + live.Capacity);
		}
		return end;
	}

	u32 ParticlePool::id_end() const
	{
		u32 end = 0;
		for (const ParticleAllocation& live : m_Live)
		{
			end = std::max(end, live.Id + 1);
		}
		return end;
	}
}
