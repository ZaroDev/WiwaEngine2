#include <pch.h>
#include "atmosphere_builder.h"

#include <chrono>

#include "../ibl/environment_pack.h"

namespace wiwa
{
	namespace
	{
		constexpr u32 ATMOSPHERE_FACE_SIZE = 64; // the sky is smooth
	}

	AtmosphereBuilder::~AtmosphereBuilder()
	{
		if (m_Job.valid())
		{
			m_Job.wait();
		}
	}

	void AtmosphereBuilder::start(const AtmosphereDescription& description)
	{
		if (m_Jobs == nullptr)
		{
			m_Error = "there is no job system";
			m_Builds++;
			return;
		}

		m_Job = m_Jobs->submit([description]()
			{
				Build build;
				build.Description = description;
				build.Data = pack_environment(build_environment(make_atmosphere_cube(description, ATMOSPHERE_FACE_SIZE), 1, 8));
				return build;
			});
	}

	void AtmosphereBuilder::request(const AtmosphereDescription& description)
	{
		if (m_Job.valid())
		{
			m_Waiting = description; // replaces an older waiting request
			return;
		}
		start(description);
	}

	std::optional<AtmosphereBuilder::Build> AtmosphereBuilder::poll()
	{
		if (!m_Job.valid() || m_Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return std::nullopt;
		}

		std::optional<Build> result;
		try
		{
			result = m_Job.get();
			m_Error.clear();
		}
		catch (const std::exception& exception)
		{
			m_Error = exception.what();
		}
		m_Builds++;

		if (m_Waiting)
		{
			const AtmosphereDescription next = *m_Waiting;
			m_Waiting.reset();
			start(next);
		}
		return result;
	}
}
