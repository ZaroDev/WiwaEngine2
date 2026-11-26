#pragma once

#include <string>

namespace wiwa
{
	// The checks of the vehicle tools that need no frame (run in --physics-panel-smoke): the .wvehicle edit model (undo, refused edits, the
	// last gear) and the wheel gizmo shapes. Empty when everything held, else what failed (each failure is logged too).
	std::string run_vehicle_editor_checks();
}
