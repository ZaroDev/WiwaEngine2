#pragma once

#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <assets/asset_manager.h>
#include <assets/job_system.h>

#include "../upload_queue.h"
#include "atmosphere_builder.h"
#include "gpu_environment.h"

namespace wiwa
{
	// The GPU side of the Environment assets, and the BRDF table that goes with every environment. Keyed by the resolved asset guid.
	// Render thread only.
	//
	// The first ask for an environment starts loading it through the AssetManager (without waiting); when it is Ready it is uploaded
	// through the UploadQueue within the frame's byte budget and the CPU copy is dropped. A broken or missing asset (or one that is not
	// an environment) is logged once and stays unusable. When the editor cooks an asset again, invalidate() loads the new version and
	// replaces the old one when it is ready: the old one keeps lighting meanwhile.
	class EnvironmentCache
	{
	public:
		EnvironmentCache(nvrhi::IDevice* device, AssetManager* assets, UploadQueue* uploads, JobSystem* jobs);
		~EnvironmentCache();

		EnvironmentCache(const EnvironmentCache&) = delete;
		EnvironmentCache& operator=(const EnvironmentCache&) = delete;

		// Null until the environment is uploaded (and for good if it failed). Asking starts the load.
		const GpuEnvironment* get(const AssetRef& environment);

		// The split-sum BRDF table (RG16F, BRDF_LUT_SIZE^2), built once on a job thread. Null until it is uploaded.
		nvrhi::ITexture* brdf_lut() const { return m_BrdfLut; }

		// Once per frame, after UploadQueue::begin_frame and before its submit.
		void update();

		// The asset with this resolved guid was cooked again (the editor's reload listener).
		void invalidate(AssetGuid resolved);

		// The atmosphere as an environment: asks for a build when the sun or the parameters moved enough from the last request (null: the
		// scene does not use the atmosphere, nothing is built), and uploads a finished build. The environment in use stays until the next
		// one is uploaded. Once per frame, with update().
		void update_atmosphere(const AtmosphereDescription* wanted);
		// What the scene wants this frame; update() acts on it (the upload has to happen inside the frame's upload window).
		void set_atmosphere_wanted(const AtmosphereDescription* wanted);
		const GpuEnvironment* atmosphere() const { return m_AtmosphereEntry.Uploaded ? &m_AtmosphereEntry.Gpu : nullptr; }
		// "none", "building", "ready" or "failed: <why>" (the previous environment stays in use after a failure).
		std::string atmosphere_status() const;
		const AtmosphereBuilder& atmosphere_builder() const { return m_Atmosphere; }

		// Environments that live in memory only, for tests and tools.
		AssetRef add_inline_environment(EnvironmentData environment);
		void set_inline_environment(const AssetRef& environment, EnvironmentData data);

		struct Stats
		{
			u32 Environments = 0; // uploaded
			u32 Pending = 0;      // loading or waiting for upload room
			u32 Failed = 0;
			u32 Reloads = 0;
			u32 AtmosphereBuilds = 0; // finished
			bool BrdfReady = false;
		};
		Stats get_stats() const;

		// "none" (no valid reference), "loading", "ready" or "failed: <why>", for the panels.
		std::string status(const AssetRef& environment) const;

	private:
		struct Entry
		{
			AssetRef Asset{};
			Handle<EnvironmentData> Loaded{};
			Ref<const EnvironmentData> Inline{};
			GpuEnvironment Gpu{};
			bool Uploaded = false;
			bool Failed = false;
			bool Reloading = false;
			std::string Error{};
		};

		void acquire(Entry& entry);
		LoadState state_of(const Entry& entry) const;
		const EnvironmentData* data_of(const Entry& entry) const;
		bool upload(Entry& entry, const EnvironmentData& data);
		void finish_brdf();

		nvrhi::IDevice* m_Device;
		AssetManager* m_Assets;
		UploadQueue* m_Uploads;

		std::unordered_map<AssetGuid, Ref<const EnvironmentData>> m_InlineEnvironments{};
		std::unordered_map<AssetGuid, std::unique_ptr<Entry>> m_Entries{};
		std::vector<Entry*> m_Pending{};

		AtmosphereBuilder m_Atmosphere;
		Entry m_AtmosphereEntry{};
		std::optional<AtmosphereDescription> m_AtmosphereRequested{};
		std::optional<AtmosphereDescription> m_AtmosphereWanted{};
		std::optional<AtmosphereBuilder::Build> m_AtmosphereWaiting{}; // built, not uploaded yet (no room)
		bool m_AtmosphereFailed = false;

		std::future<std::vector<glm::vec2>> m_BrdfJob{};
		nvrhi::TextureHandle m_BrdfLut{};

		u32 m_Reloads = 0;
		u32 m_FailedCount = 0;
	};
}
