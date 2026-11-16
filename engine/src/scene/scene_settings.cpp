#include <pch.h>
#include "scene_settings.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wiwa
{
	namespace
	{
		// Replaces a value that is not finite by the fallback and clamps the rest; true when it had to change anything.
		bool fix(float& value, float fallback, float low, float high)
		{
			const float before = value;
			if (!std::isfinite(value))
			{
				value = fallback;
			}
			value = std::clamp(value, low, high);
			return value != before || !std::isfinite(before);
		}

		bool fix(glm::vec3& value, const glm::vec3& fallback, float low, float high)
		{
			bool changed = false;
			for (int i = 0; i < 3; i++)
			{
				changed = fix(value[i], fallback[i], low, high) || changed;
			}
			return changed;
		}

		bool fix_count(u32& value, u32 low, u32 high)
		{
			const u32 before = value;
			value = std::clamp(value, low, high);
			return value != before;
		}
	}

	namespace
	{
		// Corrects each field the list in scene_settings.h visits, by its range.
		struct Sanitizer
		{
			bool Changed = false;

			template<typename E, size_t N>
			void choice(const char*, E& value, const E& fallback, const std::array<const char*, N>&)
			{
				if (static_cast<size_t>(value) >= N)
				{
					value = fallback;
					Changed = true;
				}
			}

			void asset(const char*, AssetRef&) {}

			void angle(const char*, float& value, const float& fallback)
			{
				if (!std::isfinite(value))
				{
					value = fallback;
					Changed = true;
				}
				else if (value < 0.0f || value >= 360.0f)
				{
					value = std::fmod(value, 360.0f);
					if (value < 0.0f)
					{
						value += 360.0f;
					}
					Changed = true;
				}
			}

			void number(const char*, float& value, const float& fallback, float low, float high) { Changed = fix(value, fallback, low, high) || Changed; }
			void vec3(const char*, glm::vec3& value, const glm::vec3& fallback, float low, float high) { Changed = fix(value, fallback, low, high) || Changed; }
			void flag(const char*, bool&, const bool&) {}
			void count(const char*, u32& value, const u32&, u32 low, u32 high) { Changed = fix_count(value, low, high) || Changed; }

			void scripts(const char*, std::vector<AssetRef>& list)
			{
				const size_t before = list.size();
				std::erase_if(list, [](const AssetRef& script) { return !script.valid(); });
				Changed = list.size() != before || Changed;
			}

			template<typename F>
			void group(const char*, F fn) { fn(); }
		};

		// Finds the numeric field at an address and keeps its range.
		struct RangeFinder
		{
			const void* Target = nullptr;
			SettingRange Found{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max() };

			template<typename E, size_t N>
			void choice(const char*, const E&, const E&, const std::array<const char*, N>&) {}
			void asset(const char*, const AssetRef&) {}
			void angle(const char*, const float& value, const float&)
			{
				if (&value == Target)
				{
					Found = { 0.0f, 360.0f };
				}
			}
			void number(const char*, const float& value, const float&, float low, float high)
			{
				if (&value == Target)
				{
					Found = { low, high };
				}
			}
			void vec3(const char*, const glm::vec3&, const glm::vec3&, float, float) {}
			void flag(const char*, const bool&, const bool&) {}
			void count(const char*, const u32& value, const u32&, u32 low, u32 high)
			{
				if (&value == Target)
				{
					Found = { static_cast<float>(low), static_cast<float>(high) };
				}
			}
			void scripts(const char*, const std::vector<AssetRef>&) {}
			template<typename F>
			void group(const char*, F fn) { fn(); }
		};

		SettingRange find_range(const SceneSettings& settings, const void* field)
		{
			RangeFinder finder;
			finder.Target = field;
			visit_scene_settings(settings, SceneSettings{}, finder);
			return finder.Found;
		}
	}

	SettingRange settings_range(const SceneSettings& settings, const float& field) { return find_range(settings, &field); }
	SettingRange settings_range(const SceneSettings& settings, const u32& field) { return find_range(settings, &field); }

	SceneSettings sanitized(SceneSettings s)
	{
		const SceneSettings defaults{};
		Sanitizer sanitizer;
		visit_scene_settings(s, defaults, sanitizer);

		// the limits that depend on another value, after that one was corrected
		ExposureSettings& e = s.Exposure;
		sanitizer.Changed = fix(e.MaxEv, defaults.Exposure.MaxEv, e.MinEv, 10.0f) || sanitizer.Changed;
		sanitizer.Changed = fix(e.HighPercent, defaults.Exposure.HighPercent, e.LowPercent + 0.05f, 1.0f) || sanitizer.Changed;

		if (sanitizer.Changed)
		{
			WI_LOG(Warning, "Scene", "scene settings had values that cannot be used: they were corrected");
		}
		return s;
	}
}
