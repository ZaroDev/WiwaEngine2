#pragma once

#include <functional>
#include <string>

#include <assets/asset_guid.h>
#include <assets/asset_types.h>

namespace wiwa
{
	class AssetDatabase;
}

// The widget the Inspector draws for an asset field and the Material panel for its texture slots: a read-only rounded box that takes a
// file dragged from the Asset Browser (payload "WI_ASSET") and a Clear button while it holds something. Main thread, between the
// ImGui frame's begin and end.
enum class AssetFieldEvent
{
	None,
	Dropped, // droppedPath holds the source path of the file that was dropped
	Cleared
};

// What the slot needs to say which asset it holds. Everything is optional: without a Database a reference shows its short guid.
struct AssetFieldContext
{
	wiwa::AssetType Expected = wiwa::AssetType::None; // the type the field accepts, shown as a hint and in the tooltip
	const wiwa::AssetDatabase* Database = nullptr;    // names the asset by its source file
	std::function<void(const wiwa::AssetRef&)> Locate{};         // "Show in browser"
	std::function<void(const wiwa::AssetRef&)> FindReferences{}; // "Find references"
};

// An asset reference: the icon and name of the source file, a tooltip with its path, guid and type, a red "missing" state when the
// catalog does not know the guid, a right-click menu, and a Clear button. label (may be null) is drawn after the box like other fields.
AssetFieldEvent draw_asset_slot(const char* id, const char* label, const wiwa::AssetRef& ref, const AssetFieldContext& context, std::string& droppedPath);

// A slot that holds a path or free text (the Material panel's slots): the text as given.
AssetFieldEvent draw_asset_field(const char* id, const std::string& text, bool hasValue, std::string& droppedPath);
