#include <editor_common.h>
#include "audio_event_picker.h"

#include <imgui.h>

#include <audio/audio_module.h>
#include <core/engine.h>

#include "../audio/audio_edit_model.h"

bool draw_audio_event_picker(const char* label, std::string& event)
{
	bool changed = false;
	wiwa::AudioModule* module = wiwa::Engine::get().get_module<wiwa::AudioModule>();
	const std::vector<const wiwa::AudioBankInfo*> banks = module != nullptr ? module->bank_infos() : std::vector<const wiwa::AudioBankInfo*>{};

	ImGui::PushID(label);
	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), "%s", event.c_str());
	ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 60.0f);
	if (ImGui::InputText("##event", buffer, sizeof(buffer)))
	{
		event = buffer;
		changed = true;
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("..."))
	{
		ImGui::OpenPopup("##events");
	}
	if (wiwa::audio_event_unknown(event, banks))
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(!)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("No loaded soundbank lists the event '%s'.", event.c_str());
		}
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(label);

	if (ImGui::BeginPopup("##events"))
	{
		static char filter[128] = "";
		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputTextWithHint("##filter", "Search events", filter, sizeof(filter));
		ImGui::BeginChild("##list", ImVec2(260.0f, 220.0f));
		const std::vector<wiwa::AudioEventRow> rows = wiwa::audio_event_rows(banks, filter);
		for (const wiwa::AudioEventRow& row : rows)
		{
			if (ImGui::Selectable(row.Name.c_str(), row.Name == event))
			{
				event = row.Name;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		if (rows.empty())
		{
			ImGui::TextDisabled(banks.empty() ? "No bank with events is loaded." : "No event matches.");
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return changed;
}
