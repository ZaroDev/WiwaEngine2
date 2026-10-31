#pragma once

#include <string>

// The widget the Inspector draws for an asset field and the Material panel for its texture slots: a read-only rounded text box
// that takes a file dragged from the Asset Browser (payload "WI_ASSET") and a Clear button while it holds something. Main thread,
// between the ImGui frame's begin and end.
enum class AssetFieldEvent
{
	None,
	Dropped, // droppedPath holds the source path of the file that was dropped
	Cleared
};

AssetFieldEvent draw_asset_field(const char* id, const std::string& text, bool hasValue, std::string& droppedPath);
