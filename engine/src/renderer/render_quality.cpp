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
		quality.BloomMips = std::clamp(quality.BloomMips, 2u, 6u);
		quality.AoSlices = std::clamp(quality.AoSlices, 1u, 4u);
		quality.AoSteps = std::clamp(quality.AoSteps, 4u, 16u);
		quality.Fxaa = quality.Fxaa && !quality.Taa; // FXAA after a TAA resolve only blurs it
		quality.ParticlePoolSize = std::clamp(quality.ParticlePoolSize, 4096u, 4194304u);
		quality.ParticleMaxPerEmitter = std::clamp(quality.ParticleMaxPerEmitter, 64u, 65536u);
		quality.ParticleRateScale = clamp_finite(quality.ParticleRateScale, defaults.ParticleRateScale, 0.0f, 4.0f);
		quality.LodPixelError = clamp_finite(quality.LodPixelError, defaults.LodPixelError, 0.0f, 16.0f);
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
		auto count = [&](const char* key, u32& out)
		{
			if (json.contains(key) && json[key].is_number_integer())
			{
				out = static_cast<u32>(std::clamp<i64>(json[key].get<i64>(), 0, 100));
			}
		};
		count("bloomMips", quality.BloomMips);
		count("aoSlices", quality.AoSlices);
		count("aoSteps", quality.AoSteps);
		number("particleRateScale", quality.ParticleRateScale);
		number("lodPixelError", quality.LodPixelError);
		if (json.contains("particlePoolSize") && json["particlePoolSize"].is_number_integer())
		{
			quality.ParticlePoolSize = static_cast<u32>(std::clamp<i64>(json["particlePoolSize"].get<i64>(), 0, 100000000));
		}
		if (json.contains("particleMaxPerEmitter") && json["particleMaxPerEmitter"].is_number_integer())
		{
			quality.ParticleMaxPerEmitter = static_cast<u32>(std::clamp<i64>(json["particleMaxPerEmitter"].get<i64>(), 0, 100000000));
		}
		if (json.contains("particleCollision") && json["particleCollision"].is_boolean())
		{
			quality.ParticleCollision = json["particleCollision"].get<bool>();
		}
		if (json.contains("fxaa") && json["fxaa"].is_boolean())
		{
			quality.Fxaa = json["fxaa"].get<bool>();
		}
		if (json.contains("taa") && json["taa"].is_boolean())
		{
			quality.Taa = json["taa"].get<bool>();
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
		json["bloomMips"] = quality.BloomMips;
		json["aoSlices"] = quality.AoSlices;
		json["aoSteps"] = quality.AoSteps;
		json["fxaa"] = quality.Fxaa;
		json["taa"] = quality.Taa;
		json["particlePoolSize"] = quality.ParticlePoolSize;
		json["particleMaxPerEmitter"] = quality.ParticleMaxPerEmitter;
		json["particleRateScale"] = quality.ParticleRateScale;
		json["particleCollision"] = quality.ParticleCollision;
		json["lodPixelError"] = quality.LodPixelError;

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}
		file << json.dump(2) << '\n';
		return file.good();
	}
}
