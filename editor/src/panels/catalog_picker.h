#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <assets/asset_catalog.h>
#include <assets/asset_manager.h>

// One Selectable per catalog asset whose file has `extension` (".winput"), to be called between BeginCombo and EndCombo.
// `isSelected(entry)` says which row is the current one. Returns the entry that was clicked this frame, or nullptr.
template <typename IsSelected>
const wiwa::AssetCatalog::Entry* catalog_selectables(const std::vector<wiwa::AssetCatalog::Entry>& entries, std::string_view extension, IsSelected&& isSelected)
{
	const wiwa::AssetCatalog::Entry* clicked = nullptr;
	for (const wiwa::AssetCatalog::Entry& entry : entries)
	{
		if (std::filesystem::path(entry.Path).extension().generic_string() == extension && ImGui::Selectable(entry.Path.c_str(), isSelected(entry)))
		{
			clicked = &entry;
		}
	}
	return clicked;
}
