#include <editor_common.h>
#include "audio_panel.h"

#include <imgui.h>

#include <audio/audio_module.h>
#include <core/engine.h>

#include "../audio/audio_edit_model.h"

AudioPanel::AudioPanel()
	: Panel("Audio", PanelCategory::Window)
{
	m_Open = false; // opened from the Window menu
}

void AudioPanel::on_update()
{
	// Closing the panel stops what it was playing.
	if (!m_Open && m_Auditioning)
	{
		stop_audition();
	}
}

bool AudioPanel::audition(const std::string& event)
{
	wiwa::AudioModule* module = wiwa::Engine::get().get_module<wiwa::AudioModule>();
	if (module == nullptr)
	{
		return false;
	}
	module->stop_audition();
	const wiwa::PlayingId playing = module->audition(event);
	m_Auditioning = playing.valid();
	m_Playing = m_Auditioning ? event : std::string();
	return m_Auditioning;
}

void AudioPanel::stop_audition()
{
	if (wiwa::AudioModule* module = wiwa::Engine::get().get_module<wiwa::AudioModule>())
	{
		module->stop_audition();
	}
	m_Auditioning = false;
	m_Playing.clear();
}

void AudioPanel::on_draw()
{
	if (begin_window(ImVec2(984.0f, 60.0f), ImVec2(440.0f, 520.0f)))
	{
		wiwa::AudioModule* module = wiwa::Engine::get().get_module<wiwa::AudioModule>();
		if (module == nullptr)
		{
			ImGui::TextDisabled("No audio module");
		}
		else
		{
			ImGui::Text("Backend: %s", wiwa::audio_backend_name(module->active_kind()));
			if (!module->last_error().empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", module->last_error().c_str());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(the backend is chosen in Settings, Audio)");
			ImGui::Text("Playing: %u", module->backend().playing_count());
			if (module->pending_banks() > 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%u banks loading)", module->pending_banks());
			}

			if (ImGui::CollapsingHeader("Soundbanks", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (module->banks().empty())
				{
					ImGui::TextDisabled("None loaded. Add banks in Settings, Audio.");
				}
				for (const wiwa::AudioModule::LoadedBank& bank : module->banks())
				{
					if (bank.HasInfo)
					{
						ImGui::BulletText("%s: %zu events, %zu RTPCs", bank.Name.c_str(), bank.Info.Events.size(), bank.Info.Rtpcs.size());
					}
					else
					{
						ImGui::BulletText("%s (no SoundbanksInfo.json: no event list)", bank.Name.c_str());
					}
				}
			}

			ImGui::Separator();
			ImGui::SetNextItemWidth(-90.0f);
			ImGui::InputTextWithHint("##audio_filter", "Search events", m_Filter, sizeof(m_Filter));
			ImGui::SameLine();
			if (ImGui::Button("Stop") && m_Auditioning)
			{
				stop_audition();
			}

			const std::vector<wiwa::AudioEventRow> rows = wiwa::audio_event_rows(module->bank_infos(), m_Filter);
			if (ImGui::BeginTable("##audio_events", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Event");
				ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("##audition", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableHeadersRow();
				for (const wiwa::AudioEventRow& row : rows)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(row.Name.c_str());
					ImGui::TableSetColumnIndex(1);
					if (row.Looping)
					{
						ImGui::TextDisabled("loop");
					}
					else if (row.DurationMs > 0.0f)
					{
						ImGui::Text("%.2f s", row.DurationMs / 1000.0f);
					}
					ImGui::TableSetColumnIndex(2);
					if (row.MaxAttenuation > 0.0f)
					{
						ImGui::Text("%.0f", row.MaxAttenuation);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::PushID(row.Name.c_str());
					if (ImGui::SmallButton(m_Auditioning && m_Playing == row.Name ? "Playing" : "Audition"))
					{
						audition(row.Name);
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (rows.empty())
			{
				ImGui::TextDisabled(m_Filter[0] != '\0' ? "No event matches." : "No events: the loaded banks have no metadata.");
			}
		}
	}
	ImGui::End();
}
