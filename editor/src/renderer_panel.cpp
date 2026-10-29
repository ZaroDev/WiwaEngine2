#include "renderer_panel.h"

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>

void RendererPanel::draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	const wiwa::RenderSnapshot snapshot = render->get_snapshot();

	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Renderer"))
	{
		ImGui::Text("%.1f FPS (%.2f ms)", snapshot.Fps, snapshot.FrameMs);
		ImGui::Text("API: %s", snapshot.Api.c_str());
		ImGui::Text("GPU: %s", snapshot.Adapter.c_str());

		const bool shadersFailed = snapshot.ShaderStatus == "failed";
		const bool shadersReady = snapshot.ShaderStatus == "ready";
		const ImVec4 shaderColor = shadersFailed ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : (shadersReady ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f) : ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
		ImGui::TextColored(shaderColor, "Shaders: %s", snapshot.ShaderStatus.c_str());

		ImGui::Text("Uploads: %s, %.2f MiB total, last %.2f ms", snapshot.UploadQueueName.c_str(),
			static_cast<double>(snapshot.UploadBytesTotal) / (1024.0 * 1024.0), snapshot.UploadMs);

		if (snapshot.ErrorCount > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "GPU errors: %u (see the log)", snapshot.ErrorCount);
		}

		// The setting is shown as the render thread has it; a click asks for the opposite.
		bool vsync = snapshot.Vsync;
		if (ImGui::Checkbox("Vertical sync", &vsync))
		{
			render->set_vsync(vsync);
		}

		if (ImGui::CollapsingHeader("Passes", ImGuiTreeNodeFlags_DefaultOpen))
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
		}
	}
	ImGui::End();
}
