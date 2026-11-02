#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ui_widget.h"

// The source format of a panel (.wuipanel) is JSON meant to be read, diffed and edited by hand. A widget lists its id, parent, kind
// and only the fields that differ from the defaults. The cooked asset (see archive in wasset_io) holds the same JSON.
namespace wiwa
{
	// Ordered, so that the file keeps the order of widgets and fields.
	using UiJson = nlohmann::ordered_json;

	UiJson ui_panel_to_json(const UiPanelData& panel);

	// Validates as it reads: widget kinds and values of the right type, ids that are unique and not 0, parents that exist, no parent
	// loops, a reference resolution above 0. Keys it does not know are ignored. When it returns nothing, error says what is wrong and
	// where, like "widgets[3].Opacity: not a number". Thread-safe.
	std::optional<UiPanelData> ui_panel_from_json(const UiJson& json, std::string& error);

	// Parses text first. The error of a text that is not JSON is "not valid JSON".
	std::optional<UiPanelData> ui_panel_from_text(const std::string& text, std::string& error);
}
