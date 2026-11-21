#include <pch.h>
#include "bench_recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>

#include <nlohmann/json.hpp>

#include <core/perf_stats.h>
#include <renderer/render_snapshot.h>

namespace wiwa
{
	namespace
	{
		// Three decimals are plenty for milliseconds and megabytes, and keep a minute of frames a few hundred kilobytes.
		double rounded(float value)
		{
			return std::round(static_cast<double>(value) * 1000.0) / 1000.0;
		}

		nlohmann::json array_of(const std::vector<float>& values)
		{
			nlohmann::json out = nlohmann::json::array();
			for (float value : values)
			{
				out.push_back(rounded(value));
			}
			return out;
		}

		nlohmann::json object_of(const std::map<std::string, std::vector<float>>& series, size_t frames)
		{
			nlohmann::json out = nlohmann::json::object();
			for (const auto& [name, values] : series)
			{
				std::vector<float> padded = values;
				padded.resize(frames, 0.0f); // a pass that stopped running before the end
				out[name] = array_of(padded);
			}
			return out;
		}
	}

	void BenchRecorder::begin(const Meta& meta)
	{
		*this = BenchRecorder{};
		m_Meta = meta;
	}

	void BenchRecorder::push_named(std::map<std::string, std::vector<float>>& series, const std::string& name, float value, size_t frame)
	{
		std::vector<float>& values = series[name];
		values.resize(frame, 0.0f);
		values.push_back(value);
	}

	void BenchRecorder::sample(float frameMs, const PerfStats& perf, const RenderSnapshot& snapshot)
	{
		const size_t frame = m_FrameMs.size();
		m_Time += static_cast<double>(frameMs) * 0.001;
		m_Times.push_back(static_cast<float>(m_Time));
		m_FrameMs.push_back(frameMs);
		m_RenderMs.push_back(static_cast<float>(snapshot.FrameMs));
		m_RenderCpuMs.push_back(perf.render_cpu().current());
		m_PhysicsMs.push_back(perf.physics_step().current());
		m_RamMb.push_back(perf.process_ram_mb().current());
		m_VramMb.push_back(perf.vram_used_mb().current());
		m_DrawCalls.push_back(static_cast<float>(snapshot.DrawCalls));
		m_Visible.push_back(static_cast<float>(snapshot.VisibleInstances));
		for (int level = 0; level < 4; level++)
		{
			m_Lod[level].push_back(static_cast<float>(snapshot.LodPrimitives[level]));
		}
		for (const auto& [name, series] : perf.modules())
		{
			push_named(m_Modules, name, series.current(), frame);
		}
		for (const auto& [name, ms] : snapshot.PassMs)
		{
			push_named(m_Passes, name, static_cast<float>(ms), frame);
		}

		m_Api = snapshot.Api;
		m_Adapter = snapshot.Adapter;
		m_Instances = snapshot.SceneInstances;
		m_Lights = snapshot.SceneLights;
		m_Textures = snapshot.CacheTextures;
		m_Meshes = snapshot.CacheMeshes;
		m_CacheBytes = snapshot.CacheBytes;
		m_ShadowViews = snapshot.ShadowViews;
	}

	double BenchRecorder::average_fps() const
	{
		return m_Time > 0.0 ? static_cast<double>(m_FrameMs.size()) / m_Time : 0.0;
	}

	double BenchRecorder::percentile_ms(double fraction) const
	{
		if (m_FrameMs.empty())
		{
			return 0.0;
		}
		std::vector<float> sorted = m_FrameMs;
		std::sort(sorted.begin(), sorted.end());
		return sorted[std::min(sorted.size() - 1, static_cast<size_t>(static_cast<double>(sorted.size()) * fraction))];
	}

	double BenchRecorder::worst_ms() const
	{
		return m_FrameMs.empty() ? 0.0 : static_cast<double>(*std::max_element(m_FrameMs.begin(), m_FrameMs.end()));
	}

	std::string BenchRecorder::to_json() const
	{
		using nlohmann::json;
		const size_t frames = m_FrameMs.size();

		json meta;
		meta["label"] = m_Meta.Label;
		meta["scene"] = m_Meta.Scene;
		meta["config"] = m_Meta.Config;
		meta["seconds"] = m_Meta.Seconds;
		meta["targetFps"] = m_Meta.TargetFps;
		meta["vsync"] = m_Meta.Vsync;
		meta["api"] = m_Api;
		meta["adapter"] = m_Adapter;
		meta["sceneInstances"] = m_Instances;
		meta["sceneLights"] = m_Lights;
		meta["textures"] = m_Textures;
		meta["meshes"] = m_Meshes;
		meta["cacheBytes"] = m_CacheBytes;
		meta["shadowViews"] = m_ShadowViews;
		const std::time_t now = std::time(nullptr);
		char stamp[32]{};
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);
		meta["date"] = stamp;
		json extra = json::object();
		for (const auto& [key, value] : m_Meta.Extra)
		{
			extra[key] = value;
		}
		meta["extra"] = extra;

		json summary;
		summary["frames"] = frames;
		summary["seconds"] = rounded(static_cast<float>(m_Time));
		summary["avgFps"] = rounded(static_cast<float>(average_fps()));
		summary["avgMs"] = frames > 0 ? rounded(static_cast<float>(m_Time * 1000.0 / static_cast<double>(frames))) : 0.0;
		summary["p50Ms"] = rounded(static_cast<float>(percentile_ms(0.5)));
		summary["p95Ms"] = rounded(static_cast<float>(percentile_ms(0.95)));
		summary["p99Ms"] = rounded(static_cast<float>(percentile_ms(0.99)));
		summary["p999Ms"] = rounded(static_cast<float>(percentile_ms(0.999)));
		summary["worstMs"] = rounded(static_cast<float>(worst_ms()));

		json series;
		series["t"] = array_of(m_Times);
		series["frameMs"] = array_of(m_FrameMs);
		series["renderMs"] = array_of(m_RenderMs);
		series["renderCpuMs"] = array_of(m_RenderCpuMs);
		series["physicsMs"] = array_of(m_PhysicsMs);
		series["ramMb"] = array_of(m_RamMb);
		series["vramMb"] = array_of(m_VramMb);
		series["drawCalls"] = array_of(m_DrawCalls);
		series["visibleInstances"] = array_of(m_Visible);
		for (int level = 0; level < 4; level++)
		{
			series["lod" + std::to_string(level)] = array_of(m_Lod[level]);
		}

		json root;
		root["schema"] = "wiwa-bench/1";
		root["meta"] = meta;
		root["summary"] = summary;
		root["series"] = series;
		root["modules"] = object_of(m_Modules, frames);
		root["passes"] = object_of(m_Passes, frames);
		return root.dump();
	}

	bool BenchRecorder::write_json(const std::string& path, std::string& error) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			error = "cannot write " + path;
			return false;
		}
		out << to_json();
		out.flush();
		if (!out)
		{
			error = "writing " + path + " failed";
			return false;
		}
		return true;
	}
}
