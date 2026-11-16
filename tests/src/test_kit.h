#pragma once

#include <core/log.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <glm/glm.hpp>

namespace wiwa
{
	// The shared helpers of the smoke tests (header only).

	// Counts failures and logs each one. A smoke file names its log area and message prefix with a tag:
	//   struct CheckLog { static constexpr const char* Area = "Scene"; static constexpr const char* Prefix = "Scene smoke test"; };
	//   using Check = BasicCheck<CheckLog>;
	template<typename LogTag>
	struct BasicCheck
	{
		u32 Failures = 0;

		void operator()(bool condition, const std::string& what)
		{
			if (!condition)
			{
				Failures++;
				WI_LOG(Error, LogTag::Area, "{0} FAILED: {1}", LogTag::Prefix, what);
			}
		}
	};

	// Float comparison with an absolute tolerance.
	inline bool near_equal(double a, double b, double epsilon = 1e-4)
	{
		return std::abs(a - b) <= epsilon;
	}

	// Length of the difference of two vectors under the tolerance.
	inline bool near_equal(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
	{
		return glm::length(a - b) < epsilon;
	}

	// Writes a text (or byte) file, creating its folder. False when the file could not be written.
	inline bool write_text(const std::filesystem::path& path, const std::string& text)
	{
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
		return static_cast<bool>(out);
	}

	// Polls `done` (every `step`) until it is true or `timeout` has passed. Returns false on a timeout, after logging an error:
	// a wait never lasts longer than the timeout, and the caller can pass the result to its check.
	template<typename Predicate>
	bool pump_until(Predicate done, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000), std::chrono::milliseconds step = std::chrono::milliseconds(1))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!done())
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				WI_LOG(Error, "Test", "pump_until timed out after {0} ms", static_cast<long long>(timeout.count()));
				return false;
			}
			std::this_thread::sleep_for(step);
		}
		return true;
	}
}
