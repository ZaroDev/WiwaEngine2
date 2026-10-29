#pragma once

#include <filesystem>

#include <renderer/scene/render_scene.h>

namespace wiwa
{
	// What the viewport overlays show, and how the selection is outlined. Kept per user, in gizmo_settings.json next to the program.
	struct GizmoSettings
	{
		bool Lights = true;   // markers for every light, the sun's arrow, and the range or cone of the selected light
		bool Cameras = true;  // markers and the frustum of every camera
		bool Bounds = true;   // the world bounds of the selected mesh
		SelectionOutlineStyle Outline{};

		bool operator==(const GizmoSettings&) const = default;

		static std::filesystem::path default_path() { return "gizmo_settings.json"; }

		// A file that is missing, empty, not JSON or of the wrong shape gives the defaults; keys that are missing or of the wrong type keep
		// theirs; numbers are clamped (outline width 1..8, alpha and color 0..1).
		static GizmoSettings load(const std::filesystem::path& file = default_path());

		// False when the file cannot be written.
		bool save(const std::filesystem::path& file = default_path()) const;
	};
}
