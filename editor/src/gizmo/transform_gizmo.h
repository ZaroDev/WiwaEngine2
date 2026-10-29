#pragma once

#include <optional>

#include <math/gizmo_math.h>
#include <renderer/debug/debug_draw.h>
#include <scene/scene_editor.h>

namespace wiwa
{
	// What the gizmo needs to know about the mouse and the camera in one frame, with no ImGui in it.
	struct GizmoInput
	{
		glm::vec2 MousePixel{ 0.0f };    // over the viewport image, (0, 0) at its top left, in the unit of ViewportSize
		glm::vec2 ViewportSize{ 1.0f };
		bool Hovered = false;            // the mouse is over the image, not over a widget on it, and the camera is not being flown
		bool LeftPressed = false;        // the left button went down this frame
		bool LeftDown = false;
		bool Ctrl = false;               // snap
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
	};

	// The translate / rotate / scale gizmo of the selected entity. Draws its handles through DebugDraw (always on top, 3 px, the handle under
	// the mouse and the one being dragged highlighted) and, while a handle is dragged, writes the entity's local Transform through the
	// SceneEditor (no undo). Main thread. A drag works from the state at the moment it began, so it never drifts.
	class TransformGizmo
	{
	public:
		static constexpr float SIZE_PIXELS = 110.0f; // the gizmo covers about this many pixels of the viewport, whatever the distance
		static constexpr float HIT_PIXELS = 8.0f;    // how close to a handle counts as on it

		gizmo::Mode mode = gizmo::Mode::Translate;
		gizmo::Space space = gizmo::Space::World;
		float TranslateSnap = 0.5f;       // world units
		float RotateSnapDegrees = 15.0f;
		float ScaleSnap = 0.1f;           // of the scale factor

		// Returns true while the mouse belongs to the gizmo: over one of its handles, or dragging one (a click must not also pick).
		bool update(DebugDraw& draw, const GizmoInput& input, SceneEditor& editor);

		bool dragging() const { return m_Drag.has_value(); }
		gizmo::Handle hovered_handle() const { return m_Hovered; }

	private:
		struct Drag
		{
			entt::entity Entity{ entt::null };
			gizmo::Mode Mode = gizmo::Mode::Translate;
			gizmo::Handle Handle = gizmo::Handle::None;
			gizmo::Frame Frame{};           // where the gizmo was when the drag began
			glm::vec3 StartScale{ 1.0f };    // local scale at the start
			glm::quat StartWorldRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::mat4 ParentWorld{ 1.0f };
			float GrabParameter = 0.0f;      // along the axis
			glm::vec3 GrabPoint{ 0.0f };     // on the plane
			glm::vec3 GrabVector{ 1.0f, 0.0f, 0.0f }; // from the origin, for rings
			glm::vec3 CameraRight{ 1.0f, 0.0f, 0.0f };
			glm::vec3 CameraUp{ 0.0f, 1.0f, 0.0f };
			glm::vec3 CameraForward{ 0.0f, 0.0f, -1.0f };
		};

		bool begin_drag(const gizmo::Ray& ray, const GizmoInput& input, gizmo::Handle handle, const gizmo::Frame& frame, entt::entity entity, SceneEditor& editor);
		void apply_drag(const gizmo::Ray& ray, const GizmoInput& input, SceneEditor& editor);
		void draw_handles(DebugDraw& draw, const gizmo::Frame& frame, gizmo::Handle highlighted) const;

		std::optional<Drag> m_Drag{};
		gizmo::Handle m_Hovered = gizmo::Handle::None;
	};
}
