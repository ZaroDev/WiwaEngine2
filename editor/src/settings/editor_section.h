#pragma once

#include <string>

#include <gizmo/gizmo_settings.h>

#include <string>

// The editor's own look (gizmo_settings.json, per user, next to the program): which overlays the Viewport shows and how the selection is
// outlined. The Viewport's "Gizmos" popup and the Settings page edit the same object, so neither saves over the other.
class EditorSection final
{
public:
	EditorSection() : m_Gizmos(wiwa::GizmoSettings::load()) {}

	const char* name() const { return "Editor"; }
	bool load();
	bool save();
	void apply() {} // the Viewport reads the value every frame
	const std::string& last_error() const { return m_Error; }

	const wiwa::GizmoSettings& gizmos() const { return m_Gizmos; }
	// For a widget to edit in place; follow an edit with touch().
	wiwa::GizmoSettings& gizmos_mutable() { return m_Gizmos; }
	// Marks the value as edited; it is saved by the next flush().
	void touch() { m_Dirty = true; }
	// Saves an edited value once the mouse is let go (a drag then writes the file once, not on every frame).
	void flush(bool mouseDown);

private:
	wiwa::GizmoSettings m_Gizmos{};
	bool m_Dirty = false;
	std::string m_Error{};
};
