#include <pch.h>
#include "render_quality.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	namespace
	{
		float clamp_finite(float value, float fallback, float low, float high)
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}
	}

	RenderQuality sanitized(RenderQuality quality)
	{
		const RenderQuality defaults{};
		quality.ShadowDistance = clamp_finite(quality.ShadowDistance, defaults.ShadowDistance, 1.0f, 1000.0f);
		quality.ShadowCascades = std::clamp(quality.ShadowCascades, 1u, 4u);
		quality.ShadowSplitLambda = clamp_finite(quality.ShadowSplitLambda, defaults.ShadowSplitLambda, 0.0f, 1.0f);
		quality.ShadowDepthBias = clamp_finite(quality.ShadowDepthBias, defaults.ShadowDepthBias, 0.0f, 0.05f);
		quality.ShadowNormalBias = clamp_finite(quality.ShadowNormalBias, defaults.ShadowNormalBias, 0.0f, 10.0f);
		quality.SpotShadowBias = clamp_finite(quality.SpotShadowBias, defaults.SpotShadowBias, 0.0f, 0.05f);
		return quality;
	}

	std::optional<RenderQuality> load_render_quality(const std::filesystem::path& path, std::string& error)
	{
		error.clear();
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return std::nullopt; // not there: the defaults, and nothing to report
		}

		nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			error = path.string() + ": not a JSON object";
			return std::nullopt;
		}

		RenderQuality quality;
		auto number = [&](const char* key, float& out)
		{
			if (json.contains(key) && json[key].is_number())
			{
				out = json[key].get<float>();
			}
		};
		number("shadowDistance", quality.ShadowDistance);
		number("shadowSplitLambda", quality.ShadowSplitLambda);
		number("shadowDepthBias", quality.ShadowDepthBias);
		number("shadowNormalBias", quality.ShadowNormalBias);
		number("spotShadowBias", quality.SpotShadowBias);
		if (json.contains("shadowCascades") && json["shadowCascades"].is_number_integer())
		{
			quality.ShadowCascades = static_cast<u32>(std::clamp<i64>(json["shadowCascades"].get<i64>(), 0, 100));
		}
		if (json.contains("vsync") && json["vsync"].is_boolean())
		{
			quality.Vsync = json["vsync"].get<bool>();
		}
		if (json.contains("copyQueue") && json["copyQueue"].is_boolean())
		{
			quality.CopyQueue = json["copyQueue"].get<bool>();
		}
		return sanitized(quality);
	}

	bool save_render_quality(const std::filesystem::path& path, const RenderQuality& quality)
	{
		nlohmann::ordered_json json = nlohmann::ordered_json::object();
		json["shadowDistance"] = quality.ShadowDistance;
		json["shadowCascades"] = quality.ShadowCascades;
		json["shadowSplitLambda"] = quality.ShadowSplitLambda;
		json["shadowDepthBias"] = quality.ShadowDepthBias;
		json["shadowNormalBias"] = quality.ShadowNormalBias;
		json["spotShadowBias"] = quality.SpotShadowBias;
		json["vsync"] = quality.Vsync;
		json["copyQueue"] = quality.CopyQueue;

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}
		file << json.dump(2) << '\n';
		return file.good();
	}
}
