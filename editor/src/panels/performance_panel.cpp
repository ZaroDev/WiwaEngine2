#include "performance_panel.h"

#include <algorithm>

#include <imgui.h>

#include <core/engine.h>
#include <physics/physics_module.h>
#include <renderer/render_module.h>

namespace
{
	constexpr ImU32 MainColor = IM_COL32(90, 170, 255, 255);
	constexpr ImU32 RenderColor = IM_COL32(255, 170, 60, 255);
	constexpr ImU32 PhysicsColor = IM_COL32(120, 220, 120, 255);
	constexpr ImU32 MemoryColor = IM_COL32(200, 130, 255, 255);
	constexpr ImU32 WarnColor = IM_COL32(255, 200, 60, 255);
	const ImVec4 ErrorText = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
	const ImVec4 WarnText = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
	const ImVec4 GoodText = ImVec4(0.45f, 0.9f, 0.45f, 1.0f);

	std::vector<float> values_of(const wiwa::Series& series)
	{
		std::vector<float> values(series.count());
		for (size_t i = 0; i < values.size(); ++i)
		{
			values[i] = series.at(i);
		}
		return values;
	}

	struct GraphLine
	{
		const char* Name;
		ImU32 Color;
		const std::vector<float>* Values;
	};

	struct GraphReference
	{
		float Value;
		const char* Label;
	};

	// Polylines of the latest samples, newest at the right edge, on a plain background. The Y axis runs from 0 to yMax, or to the largest sample (never
	// below yFloor) when yMax is 0. Hovering shows the samples under the cursor.
	void draw_graph(const char* id, float height, const std::vector<GraphLine>& lines, float yMax, float yFloor, const char* unit, const std::vector<GraphReference>& references)
	{
		const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 50.0f), height);
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + size.x, min.y + size.y);
		ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();

		float top = yMax;
		if (top <= 0.0f)
		{
			for (const GraphLine& line : lines)
			{
				for (const float value : *line.Values)
				{
					top = std::max(top, value);
				}
			}
			top = std::max(top * 1.1f, yFloor);
		}

		top = std::max(top, 0.001f);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(min, max, IM_COL32(20, 20, 24, 255));
		draw->AddRect(min, max, IM_COL32(70, 70, 80, 255));

		const float step = size.x / static_cast<float>(wiwa::Series::Capacity - 1);
		const auto y_of = [&](float value) { return max.y - std::clamp(value / top, 0.0f, 1.0f) * size.y; };

		for (const GraphReference& reference : references)
		{
			if (reference.Value < top)
			{
				const float y = y_of(reference.Value);
				draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), IM_COL32(255, 255, 255, 40));
				draw->AddText(ImVec2(min.x + 4.0f, y - ImGui::GetTextLineHeight()), IM_COL32(255, 255, 255, 110), reference.Label);
			}
		}

		std::vector<ImVec2> points;
		for (const GraphLine& line : lines)
		{
			const size_t count = line.Values->size();
			if (count < 2)
			{
				continue;
			}
			points.resize(count);
			for (size_t i = 0; i < count; ++i)
			{
				points[i] = ImVec2(max.x - static_cast<float>(count - 1 - i) * step, y_of((*line.Values)[i]));
			}
			draw->AddPolyline(points.data(), static_cast<int>(count), line.Color, ImDrawFlags_None, 1.5f);
		}

		char top_label[32];
		snprintf(top_label, sizeof(top_label), "%.1f %s", top, unit);
		draw->AddText(ImVec2(max.x - ImGui::CalcTextSize(top_label).x - 4.0f, min.y + 2.0f), IM_COL32(255, 255, 255, 140), top_label);

		if (hovered)
		{
			const size_t back = static_cast<size_t>(std::max(0.0f, std::round((max.x - ImGui::GetIO().MousePos.x) / step)));
			ImGui::BeginTooltip();
			for (const GraphLine& line : lines)
			{
				const size_t count = line.Values->size();
				if (back < count)
				{
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(line.Color), "%s: %.2f %s", line.Name, (*line.Values)[count - 1 - back], unit);
				}
			}
			ImGui::EndTooltip();
		}
	}

	void thread_row(const char* name, const wiwa::Series& series)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(name);
		ImGui::TableNextColumn();
		if (series.count() == 0)
		{
			ImGui::TextDisabled("unavailable");
			return;
		}
		ImGui::Text("%.2f ms", series.current());
		ImGui::TableNextColumn();
		ImGui::Text("%.2f", series.average());
		ImGui::TableNextColumn();
		ImGui::Text("%.2f", series.max());
	}
}

void PerformancePanel::on_draw()
{
	wiwa::Engine& engine = wiwa::Engine::get();
	const wiwa::PerfStats& perf = engine.get_perf();
	wiwa::RenderModule* render = engine.get_module<wiwa::RenderModule>();
	const bool renderReady = render != nullptr && render->is_ready();
	const wiwa::RenderSnapshot snapshot = renderReady ? render->get_snapshot() : wiwa::RenderSnapshot{};

	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(get_name(), &m_Open))
	{
		ImGui::End();
		return;
	}

	// header
	if (renderReady)
	{
		ImGui::Text("%.1f FPS (%.2f ms)", snapshot.Fps, snapshot.FrameMs);
		ImGui::Text("API: %s", snapshot.Api.c_str());
		ImGui::Text("GPU: %s", snapshot.Adapter.c_str());
		const bool shadersFailed = snapshot.ShaderStatus == "failed";
		const bool shadersReady = snapshot.ShaderStatus == "ready";
		ImGui::TextColored(shadersFailed ? ErrorText : (shadersReady ? GoodText : WarnText), "Shaders: %s", snapshot.ShaderStatus.c_str());
		if (snapshot.ErrorCount > 0)
		{
			ImGui::TextColored(ErrorText, "GPU errors: %u (see the log)", snapshot.ErrorCount);
		}
	}
	else
	{
		ImGui::TextDisabled("Renderer: unavailable");
	}

	// threads
	if (ImGui::CollapsingHeader("Threads", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("threads", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Thread");
			ImGui::TableSetupColumn("Now");
			ImGui::TableSetupColumn("Avg");
			ImGui::TableSetupColumn("Max");
			ImGui::TableHeadersRow();
			thread_row("Main", perf.main_frame());
			thread_row("Renderer (frame)", perf.render_frame());
			thread_row("Renderer (CPU)", perf.render_cpu());
			thread_row("Physics (step)", perf.physics_step());
			ImGui::EndTable();
		}

		if (ImGui::TreeNode("Main thread by module"))
		{
			std::vector<const std::pair<std::string, wiwa::Series>*> modules;
			for (const auto& module : perf.modules())
			{
				modules.push_back(&module);
			}
			std::sort(modules.begin(), modules.end(), [](const auto* a, const auto* b) { return a->second.average() > b->second.average(); });
			if (ImGui::BeginTable("modules", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				for (const auto* module : modules)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(module->first.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%.3f ms", module->second.current());
					ImGui::TableNextColumn();
					ImGui::TextDisabled("avg %.3f", module->second.average());
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Renderer passes"))
		{
			if (snapshot.PassMs.empty())
			{
				ImGui::TextDisabled("No pass has rendered yet");
			}
			else if (ImGui::BeginTable("passes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				for (const auto& [name, milliseconds] : snapshot.PassMs)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%.3f ms", milliseconds);
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}

	// frame times
	if (ImGui::CollapsingHeader("Frame time", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Pause", &m_Paused);
		// while paused the copies made on the last unpaused frame stay
		if (!m_Paused)
		{
			m_Frozen[0] = values_of(perf.main_frame());
			m_Frozen[1] = values_of(perf.render_frame());
			m_Frozen[2] = values_of(perf.physics_step());
		}
		draw_graph("##frame", 140.0f, { { "Main", MainColor, &m_Frozen[0] }, { "Renderer", RenderColor, &m_Frozen[1] }, { "Physics", PhysicsColor, &m_Frozen[2] } },
			0.0f, 33.4f, "ms", { { 16.6f, "60 fps" }, { 33.3f, "30 fps" } });
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MainColor), "Main");
		ImGui::SameLine();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(RenderColor), "Renderer");
		ImGui::SameLine();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PhysicsColor), "Physics");
	}

	// memory
	if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const std::vector<float> ram = values_of(perf.process_ram_mb());
		if (ram.empty())
		{
			ImGui::TextDisabled("RAM: unavailable");
		}
		else
		{
			ImGui::Text("RAM: %.0f MiB", ram.back());
			draw_graph("##ram", 70.0f, { { "RAM", MemoryColor, &ram } }, 0.0f, 64.0f, "MiB", {});
		}

		if (!perf.vram_available() || perf.vram_used_mb().count() == 0)
		{
			ImGui::TextDisabled("VRAM: unavailable");
		}
		else
		{
			const std::vector<float> used = values_of(perf.vram_used_mb());
			const float budget = perf.vram_budget_mb().current();
			const bool high = budget > 0.0f && used.back() > 0.85f * budget;
			ImGui::TextColored(high ? WarnText : ImGui::GetStyleColorVec4(ImGuiCol_Text), "VRAM: %.0f / %.0f MiB", used.back(), budget);
			draw_graph("##vram", 70.0f, { { "VRAM", high ? WarnColor : MemoryColor, &used } }, budget, 0.0f, "MiB", { { budget * 0.85f, "85%" } });
		}
	}

	// the renderer's counters
	if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (renderReady)
		{
			ImGui::Text("Uploads: %s, %.2f MiB total, last %.2f ms", snapshot.UploadQueueName.c_str(),
				static_cast<double>(snapshot.UploadBytesTotal) / (1024.0 * 1024.0), snapshot.UploadMs);
			ImGui::Text("Environment: %s", snapshot.EnvironmentStatus.c_str());
			ImGui::Text("Scene: %u instances (%u visible, %u draws), %u lights, %u shadow maps", snapshot.SceneInstances, snapshot.VisibleInstances, snapshot.DrawCalls,
				snapshot.SceneLights, snapshot.ShadowViews);
			ImGui::Text("GPU assets: %u meshes, %u materials, %u textures, %.1f MiB", snapshot.CacheMeshes, snapshot.CacheMaterials, snapshot.CacheTextures,
				static_cast<double>(snapshot.CacheBytes) / (1024.0 * 1024.0));
			if (snapshot.CachePending > 0 || snapshot.CacheFailed > 0)
			{
				ImGui::TextColored(snapshot.CacheFailed > 0 ? ErrorText : WarnText, "%u loading, %u failed", snapshot.CachePending, snapshot.CacheFailed);
			}
		}

		if (const wiwa::PhysicsModule* physics = engine.get_module<wiwa::PhysicsModule>())
		{
			const wiwa::PhysicsModule::Stats stats = physics->stats();
			if (stats.Running)
			{
				ImGui::Text("Physics: %u bodies (%u active), %u steps last frame, %llu dropped", stats.Bodies, stats.ActiveBodies, perf.physics_steps_last_frame(),
					static_cast<unsigned long long>(stats.DroppedSteps));
			}
			else
			{
				ImGui::TextDisabled("Physics: no world");
			}
		}
	}
	ImGui::End();
}
