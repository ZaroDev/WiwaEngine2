#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <physics/vehicle_settings.h>

// The source format of a vehicle (.wvehicle): JSON, "version": 1, then objects (every key optional, a missing one takes the default):
//   "engine":       { "torqueCurve": [[rpm, Nm], ...], "minRpm", "maxRpm", "inertia", "damping" }
//   "transmission": { "automatic": bool, "gears": [ratio, ...], "reverseGears": [...], "shiftUpRpm", "shiftDownRpm", "shiftTime", "clutchStrength" }
//   "differentials": [ { "axle", "ratio", "limitedSlipRatio", "engineTorqueRatio" }, ... ]
//   "antiRoll":     [stiffness per axle, ...]
//   "frontTire", "rearTire": { "longitudinal": { "B", "C", "D", "E" }, "lateral": { ... }, "nominalLoad", "loadSensitivity", "pneumaticTrail" }
//   "aero":         { "dragArea", "downforceArea", "downforceFront" }
//   "brakes":       { "maxTorque", "handbrakeTorque", "biasFront" }
//   "assists":      { "tractionControl", "abs", "stability", "steeringHelp" }
//   "steering":     { "speed", "limit": [[m/s, fraction], ...] }
namespace wiwa
{
	// False with a reason in error (naming the key) when the text is not JSON, a value has the wrong type, or the result is not valid
	// (vehicle_settings_valid).
	bool vehicle_from_text(const std::string& text, VehicleSettingsData& out, std::string& error);
	nlohmann::json vehicle_to_json(const VehicleSettingsData& data);
}
