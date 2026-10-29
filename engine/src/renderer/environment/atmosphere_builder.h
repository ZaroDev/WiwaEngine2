#pragma once

#include <future>
#include <optional>
#include <string>

#include <assets/asset_data.h>
#include <assets/job_system.h>

#include "../ibl/atmosphere_environment.h"

namespace wiwa
{
	// Builds the environment (cube, prefiltered mips, SH) of an atmosphere on a job thread. Render thread owner; nothing here touches the
	// GPU. A request replaces one that has not started; the one that is running finishes first.
	class AtmosphereBuilder
	{
	public:
		struct Build
		{
			AtmosphereDescription Description{};
			EnvironmentData Data{};
		};

		explicit AtmosphereBuilder(JobSystem* jobs) : m_Jobs(jobs) {}
		~AtmosphereBuilder();

		AtmosphereBuilder(const AtmosphereBuilder&) = delete;
		AtmosphereBuilder& operator=(const AtmosphereBuilder&) = delete;

		void request(const AtmosphereDescription& description);
		// A finished build, once: also starts the waiting request, if any.
		std::optional<Build> poll();

		bool building() const { return m_Job.valid() || m_Waiting.has_value(); }
		u32 builds() const { return m_Builds; }           // finished and handed out (or failed)
		const std::string& error() const { return m_Error; } // of the last build, empty when it worked

	private:
		void start(const AtmosphereDescription& description);

		JobSystem* m_Jobs;
		std::future<Build> m_Job{};
		std::optional<AtmosphereDescription> m_Waiting{};
		u32 m_Builds = 0;
		std::string m_Error{};
	};
}
