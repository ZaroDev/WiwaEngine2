#include <editor_common.h>
#include "asset_field.h"

#include <imgui.h>

#include <asset_pipeline/asset_database.h>

#include "asset_icons.h"

namespace
{
	const ImVec4 MISSING_COLOR(1.00f, 0.40f, 0.40f, 1.0f);
	const ImU32 ACCEPT_COLOR = IM_COL32(110, 180, 255, 255);

	bool asset_payload_dragged()
	{
		const ImGuiPayload* payload = ImGui::GetDragDropPayload();
		return payload != nullptr && payload->IsDataType("WI_ASSET");
	}

	// The border while an asset is dragged over the slot.
	void highlight_if_dragged_over()
	{
		if (asset_payload_dragged() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
		{
			ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ACCEPT_COLOR, 20.0f, 0, 2.0f);
		}
	}

	std::string short_guid(const wiwa::AssetRef& ref)
	{
		return ref.Guid.to_string().substr(0, 8);
	}
}

AssetFieldEvent draw_asset_slot(const char* id, const char* label, const wiwa::AssetRef& ref, const AssetFieldContext& context, std::string& droppedPath)
{
	AssetFieldEvent result = AssetFieldEvent::None;

	std::optional<std::filesystem::path> path;
	if (ref.valid() && context.Database != nullptr)
	{
		path = context.Database->path_of(ref.Guid);
	}
	const bool missing = ref.valid() && context.Database != nullptr && !path.has_value();

	std::string text;
	if (!ref.valid())
	{
		text = context.Expected != wiwa::AssetType::None ? std::string("None (") + wiwa::asset_type_name(context.Expected) + ")" : std::string("None");
	}
	else if (missing)
	{
		text = std::string(ICON_FK_EXCLAMATION_TRIANGLE) + " (missing) " + short_guid(ref);
	}
	else if (path)
	{
		text = std::string(get_filetype_icon(*path)) + " " + path->filename().string();
		if (ref.LocalId != 0)
		{
			text += " #" + std::to_string(ref.LocalId);
		}
	}
	else
	{
		text = short_guid(ref) + (ref.LocalId != 0 ? ":" + std::to_string(ref.LocalId) : std::string());
	}

	ImGui::PushID(id);
	const float clearWidth = ref.valid() ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
	ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - clearWidth));

	const bool tinted = missing || !ref.valid();
	if (missing)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, MISSING_COLOR);
	}
	else if (!ref.valid())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
	ImGui::InputText("##slot", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
	ImGui::PopStyleVar();
	if (tinted)
	{
		ImGui::PopStyleColor();
	}

	highlight_if_dragged_over();
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("WI_ASSET"))
		{
			droppedPath = reinterpret_cast<const char*>(payload->Data);
			result = AssetFieldEvent::Dropped;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !asset_payload_dragged())
	{
		ImGui::BeginTooltip();
		if (!ref.valid())
		{
			ImGui::TextUnformatted("Empty");
			if (context.Expected != wiwa::AssetType::None)
			{
				ImGui::Text("Accepts: %s", wiwa::asset_type_name(context.Expected));
			}
			ImGui::TextDisabled("Drag an asset from the Asset Browser");
		}
		else
		{
			if (missing)
			{
				ImGui::TextColored(MISSING_COLOR, "Missing: the project has no asset with this guid");
			}
			else if (path)
			{
				ImGui::TextUnformatted(path->generic_string().c_str());
			}
			ImGui::Text("Guid: %s", ref.Guid.to_string().c_str());
			if (ref.LocalId != 0)
			{
				ImGui::Text("Sub-asset: %llu", static_cast<unsigned long long>(ref.LocalId));
			}
			if (context.Expected != wiwa::AssetType::None)
			{
				ImGui::Text("Type: %s", wiwa::asset_type_name(context.Expected));
			}
		}
		ImGui::EndTooltip();
	}

	if (ImGui::BeginPopupContextItem("##slot_menu"))
	{
		if (ImGui::MenuItem("Show in browser", nullptr, false, ref.valid() && !missing && context.Locate != nullptr))
		{
			context.Locate(ref);
		}
		if (ImGui::MenuItem("Find references", nullptr, false, ref.valid() && context.FindReferences != nullptr))
		{
			context.FindReferences(ref);
		}
		if (context.OpenInAssetEditor != nullptr && (context.Expected == wiwa::AssetType::Skeleton || context.Expected == wiwa::AssetType::Mesh || context.Expected == wiwa::AssetType::AnimationClip)
			&& ImGui::MenuItem("Open in Asset Editor", nullptr, false, ref.valid() && !missing))
		{
			context.OpenInAssetEditor(ref);
		}
		if (ImGui::MenuItem("Copy guid", nullptr, false, ref.valid()))
		{
			ImGui::SetClipboardText(ref.Guid.to_string().c_str());
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Clear", nullptr, false, ref.valid()))
		{
			result = AssetFieldEvent::Cleared;
		}
		ImGui::EndPopup();
	}

	if (ref.valid())
	{
		ImGui::SameLine();
		if (ImGui::Button(ICON_FK_TIMES, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
		{
			result = AssetFieldEvent::Cleared;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip("Clear");
		}
	}
	if (label != nullptr && label[0] != '\0')
	{
		ImGui::SameLine();
		ImGui::TextUnformatted(label);
	}
	ImGui::PopID();
	return result;
}

AssetFieldEvent draw_asset_field(const char* id, const std::string& text, bool hasValue, std::string& droppedPath)
{
	AssetFieldEvent result = AssetFieldEvent::None;

	ImGui::PushID(id);
	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
	ImGui::InputText("##label", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
	ImGui::PopStyleVar();
	highlight_if_dragged_over();
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
