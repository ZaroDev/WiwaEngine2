#pragma once

#include <string>

// One page of the Settings panel without any ImGui: it owns a project-wide value (and the file it is saved to), changes it, saves it at
// once and tells the module that uses it. The panel only draws rows and calls the section's setters. Main thread.
class ISettingsSection
{
public:
	virtual ~ISettingsSection() = default;

	virtual const char* name() const = 0;
	// Reads the value again from where it is saved. False, and why in last_error, if that failed (the value is then unchanged).
	virtual bool load() = 0;
	// Writes the value to its file. False, and why in last_error, if that failed.
	virtual bool save() = 0;
	// Gives the value to the module that uses it (the running world, the window title...).
	virtual void apply() = 0;
	virtual const std::string& last_error() const = 0;
};
