#include <editor_common.h>
#include "asset_field.h"

#include <imgui.h>

AssetFieldEvent draw_asset_field(const char* id, const std::string& text, bool hasValue, std::string& droppedPath)
{
	AssetFieldEvent result = AssetFieldEvent::None;

	ImGui::PushID(id);
	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
	ImGui::InputText("##label", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
	ImGui::PopStyleVar();
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("WI_ASSET"))
		{
			droppedPath = reinterpret_cast<const char*>(payload->Data);
			result = AssetFieldEvent::Dropped;
		}
		ImGui::EndDragDropTarget();
	}
	if (hasValue)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear"))
		{
			result = AssetFieldEvent::Cleared;
		}
	}
	ImGui::PopID();
	return result;
}
