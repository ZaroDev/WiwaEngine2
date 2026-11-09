#pragma once

#include <string>

#include <renderer/render_quality.h>

#include "settings_section.h"

// The renderer's quality (render_quality.json, read from next to the program). The render module saves the file itself when a value
// changes and hands the value to the render thread, so save() and apply() have nothing more to do.
class RendererSection final : public ISettingsSection
{
public:
	virtual const char* name() const override { return "Renderer"; }
	virtual bool load() override { return true; } // the render module read the file when it started
	virtual bool save() override { return true; }
	virtual void apply() override {}
	virtual const std::string& last_error() const override { return m_Error; }

	// False while the renderer is not running (there is no quality to show).
	bool ready() const;
	wiwa::RenderQuality quality() const;
	// Sanitizes, applies and saves (the render module does all three).
	void set_quality(const wiwa::RenderQuality& quality);

private:
	std::string m_Error{};
};
