#include <pch.h>
#include "environment_cache.h"

#include <algorithm>
#include <chrono>

#include <glm/gtc/packing.hpp>

#include "../ibl/ibl_math.h"

#define WI_ENV_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// The sample count of the BRDF table: it is built once, a few hundred milliseconds on a worker.
		constexpr u32 BRDF_SAMPLES = 256;
	}

	EnvironmentCache::EnvironmentCache(nvrhi::IDevice* device, AssetManager* assets, UploadQueue* uploads, JobSystem* jobs)
		: m_Device(device)
		, m_Assets(assets)
		, m_Uploads(uploads)
		, m_Atmosphere(jobs)
	{
		if (jobs != nullptr)
		{
			m_BrdfJob = jobs->submit([]() { return make_brdf_lut(BRDF_LUT_SIZE, BRDF_SAMPLES); });
		}
	}

	EnvironmentCache::~EnvironmentCache()
	{
		if (m_BrdfJob.valid())
		{
			m_BrdfJob.wait(); // the job owns nothing of ours, but the future must not outlive its thread's work
		}
	}

	LoadState EnvironmentCache::state_of(const Entry& entry) const
	{
		return entry.Inline ? LoadState::Ready : entry.Loaded.state();
	}

	const EnvironmentData* EnvironmentCache::data_of(const Entry& entry) const
	{
		return entry.Inline ? entry.Inline.get() : entry.Loaded.get();
	}

	void EnvironmentCache::acquire(Entry& entry)
	{
		entry.Loaded = Handle<EnvironmentData>{};
		entry.Inline.reset();
		const auto found = m_InlineEnvironments.find(entry.Asset.resolved());
		if (found != m_InlineEnvironments.end())
		{
			entry.Inline = found->second;
		}
		else if (m_Assets != nullptr)
		{
			entry.Loaded = m_Assets->load<EnvironmentData>(entry.Asset);
		}
	}

	const GpuEnvironment* EnvironmentCache::get(const AssetRef& environment)
	{
		if (!environment.valid())
		{
			return nullptr;
		}

		const AssetGuid guid = environment.resolved();
		auto found = m_Entries.find(guid);
		if (found == m_Entries.end())
		{
			auto entry = std::make_unique<Entry>();
			entry->Asset = environment;
			acquire(*entry);
			m_Pending.push_back(entry.get());
			found = m_Entries.emplace(guid, std::move(entry)).first;
		}
		return found->second->Uploaded ? &found->second->Gpu : nullptr;
	}

	bool EnvironmentCache::upload(Entry& entry, const EnvironmentData& data)
	{
		nvrhi::TextureDesc description;
		description.width = data.FaceSize;
		description.height = data.FaceSize;
		description.arraySize = 6;
		description.dimension = nvrhi::TextureDimension::TextureCube;
		// A texture has at most log2(size) + 1 levels; a cooked chain that is longer (a small cube) loses its 1x1 repeats.
		u32 mipCount = 1;
		while (mipCount < data.Mips.size() && (data.FaceSize >> mipCount) >= 1)
		{
			mipCount++;
		}
		description.mipLevels = mipCount;
		description.format = nvrhi::Format::RGBA16_FLOAT;
		description.isShaderResource = true;
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "Environment cube";
		nvrhi::TextureHandle cube = m_Device->createTexture(description);
		if (!cube)
		{
			return false;
		}

		for (u32 mip = 0; mip < mipCount; mip++)
		{
			const u32 size = std::max(data.FaceSize >> mip, 1u);
			const size_t faceTexels = static_cast<size_t>(size) * size;
			for (u32 face = 0; face < 6; face++)
			{
				m_Uploads->write_texture(cube, face, mip, data.Mips[mip].data() + face * faceTexels * 4, static_cast<size_t>(size) * 8);
			}
		}

		entry.Gpu.Cube = std::move(cube); // the old one, if any, lives on in the frames that still use it
		entry.Gpu.MipCount = mipCount;
		entry.Gpu.SH = data.SH;
		entry.Uploaded = true;
		return true;
	}

	void EnvironmentCache::finish_brdf()
	{
		if (!m_BrdfJob.valid() || m_BrdfJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return;
		}

		const std::vector<glm::vec2> table = m_BrdfJob.get();
		std::vector<u16> halves;
		halves.reserve(table.size() * 2);
		for (const glm::vec2& value : table)
		{
			halves.push_back(glm::packHalf1x16(value.x));
			halves.push_back(glm::packHalf1x16(value.y));
		}

		nvrhi::TextureDesc description;
		description.width = BRDF_LUT_SIZE;
		description.height = BRDF_LUT_SIZE;
		description.format = nvrhi::Format::RG16_FLOAT;
		description.isShaderResource = true;
		description.initialState = UploadQueue::resting_state(m_Device);
		description.keepInitialState = true;
		description.debugName = "BRDF table";
		nvrhi::TextureHandle texture = m_Device->createTexture(description);
		if (!texture)
		{
			WI_ENV_LOG(Error, "could not create the BRDF table: environments have no specular light");
			return;
		}
		m_Uploads->write_texture(texture, 0, 0, halves.data(), static_cast<size_t>(BRDF_LUT_SIZE) * 4);
		m_BrdfLut = std::move(texture);
	}

	void EnvironmentCache::set_atmosphere_wanted(const AtmosphereDescription* wanted)
	{
		if (wanted != nullptr)
		{
			m_AtmosphereWanted = *wanted;
		}
		else
		{
			m_AtmosphereWanted.reset();
		}
	}

	void EnvironmentCache::update()
	{
		finish_brdf();
		update_atmosphere(m_AtmosphereWanted ? &*m_AtmosphereWanted : nullptr);

		for (size_t i = 0; i < m_Pending.size();)
		{
			Entry& entry = *m_Pending[i];
			const LoadState state = state_of(entry);
			bool finished = false;

			if (state == LoadState::Failed)
			{
				if (!entry.Uploaded)
				{
					entry.Failed = true;
					entry.Error = "the asset is missing, broken, or not an environment";
					m_FailedCount++;
					WI_ENV_LOG(Error, "environment {0} failed to load: {1}", entry.Asset.Guid.to_string(), entry.Error);
				}
				else
				{
					WI_ENV_LOG(Warning, "environment {0} could not be reloaded, the old version stays", entry.Asset.Guid.to_string());
				}
				entry.Reloading = false;
				finished = true;
			}
			else if (state == LoadState::Ready)
			{
				const EnvironmentData* data = data_of(entry);
				size_t bytes = 0;
				for (const std::vector<u16>& mip : data->Mips)
				{
					bytes += mip.size() * sizeof(u16);
				}

				if (m_Uploads->has_room(bytes))
				{
					if (upload(entry, *data))
					{
						if (entry.Reloading)
						{
							m_Reloads++;
						}
					}
					else if (!entry.Uploaded)
					{
						entry.Failed = true;
						entry.Error = "the GPU refused the cube texture";
						m_FailedCount++;
						WI_ENV_LOG(Error, "environment {0}: {1}", entry.Asset.Guid.to_string(), entry.Error);
					}
					entry.Reloading = false;
					finished = true;
				}
			}

			if (finished)
			{
				entry.Loaded = Handle<EnvironmentData>{}; // the CPU copy can go
				entry.Inline.reset();
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}
	}

	void EnvironmentCache::invalidate(AssetGuid resolved)
	{
		const auto found = m_Entries.find(resolved);
		if (found == m_Entries.end())
		{
			return;
		}

		Entry& entry = *found->second;
		acquire(entry);
		entry.Reloading = entry.Uploaded;
		if (entry.Failed)
		{
			entry.Failed = false;
			entry.Error.clear();
			m_FailedCount--;
		}
		if (std::find(m_Pending.begin(), m_Pending.end(), &entry) == m_Pending.end())
		{
			m_Pending.push_back(&entry);
		}
	}

	void EnvironmentCache::update_atmosphere(const AtmosphereDescription* wanted)
	{
		if (wanted != nullptr && (!m_AtmosphereRequested || !atmosphere_is_close(*m_AtmosphereRequested, *wanted)))
		{
			m_AtmosphereRequested = *wanted;
			m_Atmosphere.request(*wanted);
		}

		if (std::optional<AtmosphereBuilder::Build> build = m_Atmosphere.poll())
		{
			m_AtmosphereWaiting = std::move(build);
		}
		m_AtmosphereFailed = !m_Atmosphere.error().empty();

		if (m_AtmosphereWaiting)
		{
			size_t bytes = 0;
			for (const std::vector<u16>& mip : m_AtmosphereWaiting->Data.Mips)
			{
				bytes += mip.size() * sizeof(u16);
			}
			if (m_Uploads->has_room(bytes))
			{
				if (!upload(m_AtmosphereEntry, m_AtmosphereWaiting->Data))
				{
					WI_ENV_LOG(Error, "the GPU refused the atmosphere cube: the previous environment stays");
					m_AtmosphereFailed = true;
				}
				m_AtmosphereWaiting.reset();
			}
		}
	}

	std::string EnvironmentCache::atmosphere_status() const
	{
		if (m_AtmosphereFailed)
		{
			return "failed: " + (m_Atmosphere.error().empty() ? std::string("the GPU refused the cube") : m_Atmosphere.error());
		}
		if (!m_AtmosphereRequested)
		{
			return "none";
		}
		return m_Atmosphere.building() || m_AtmosphereWaiting ? (m_AtmosphereEntry.Uploaded ? "ready (rebuilding)" : "building") : "ready";
	}

	AssetRef EnvironmentCache::add_inline_environment(EnvironmentData environment)
	{
		AssetRef ref;
		ref.Guid = AssetGuid::generate();
		m_InlineEnvironments[ref.resolved()] = std::make_shared<const EnvironmentData>(std::move(environment));
		return ref;
	}

	void EnvironmentCache::set_inline_environment(const AssetRef& environment, EnvironmentData data)
	{
		m_InlineEnvironments[environment.resolved()] = std::make_shared<const EnvironmentData>(std::move(data));
	}

	EnvironmentCache::Stats EnvironmentCache::get_stats() const
	{
		Stats stats;
		for (const auto& [guid, entry] : m_Entries)
		{
			stats.Environments += entry->Uploaded ? 1 : 0;
		}
		stats.Pending = static_cast<u32>(m_Pending.size());
		stats.Failed = m_FailedCount;
		stats.Reloads = m_Reloads;
		stats.AtmosphereBuilds = m_Atmosphere.builds();
		stats.BrdfReady = m_BrdfLut != nullptr;
		return stats;
	}

	std::string EnvironmentCache::status(const AssetRef& environment) const
	{
		if (!environment.valid())
		{
			return "none";
		}
		const auto found = m_Entries.find(environment.resolved());
		if (found == m_Entries.end())
		{
			return "loading";
		}
		const Entry& entry = *found->second;
		if (entry.Failed)
		{
			return "failed: " + entry.Error;
		}
		return entry.Uploaded && !entry.Reloading ? "ready" : (entry.Uploaded ? "ready (reloading)" : "loading");
	}
}
