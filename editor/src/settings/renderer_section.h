#pragma once

#include <string>

#include <renderer/render_quality.h>

#include <string>

// The renderer's quality (render_quality.json, read from next to the program). The render module saves the file itself when a value
// changes and hands the value to the render thread, so save() and apply() have nothing more to do.
class RendererSection final
{
public:
	const char* name() const { return "Renderer"; }
	bool load() { return true; } // the render module read the file when it started
	bool save() { return true; }
	void apply() {}
	const std::string& last_error() const { return m_Error; }

	// False while the renderer is not running (there is no quality to show).
	bool ready() const;
	wiwa::RenderQuality quality() const;
	// Sanitizes, applies and saves (the render module does all three).
	void set_quality(const wiwa::RenderQuality& quality);

private:
	std::string m_Error{};
};
