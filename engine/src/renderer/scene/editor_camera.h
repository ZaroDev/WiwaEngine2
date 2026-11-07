#pragma once

#include <math/math.h>

#include "render_scene.h"

namespace wiwa
{
	// What the user did this frame, already read from whatever input there is: the camera only turns it into motion.
	struct EditorCameraInput
	{
		glm::vec2 Look{ 0.0f };  // mouse movement in pixels while looking (right button held)
		glm::vec3 Move{ 0.0f };  // x right, y up (world), z forward; each -1..1
		bool Fast = false;       // faster movement
		float Scroll = 0.0f;     // wheel notches: changes the speed
	};

	// A fly camera for looking around a scene in the editor, independent of the scene's own cameras. Pure logic: no input
	// system or UI in here.
	class EditorCamera
	{
	public:
		EditorCamera() { reset(); }

		void reset();
		void apply(float deltaSeconds, const EditorCameraInput& input);
		// Moves back along the view direction so that the point is in the middle of the view, distance away.
		void focus_on(const glm::vec3& point, float distance) { m_Position = point - forward() * distance; }

		// The camera as the renderer takes it.
		RenderCamera get(float aspect) const;

		glm::vec3 position() const { return m_Position; }
		glm::vec3 forward() const;
		float pitch_degrees() const { return glm::degrees(m_Pitch); }
		float speed() const { return m_Speed; }

	private:
		glm::vec3 m_Position{ 0.0f };
		float m_Yaw = 0.0f;   // radians, 0 looks along -Z
		float m_Pitch = 0.0f; // radians, limited to +-89 degrees
		float m_Speed = 4.0f; // units per second
	};
}
