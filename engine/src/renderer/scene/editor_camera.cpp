#include <pch.h>
#include "editor_camera.h"

#include <algorithm>

#include "scene_extractor.h"

namespace wiwa
{
	namespace
	{
		constexpr float LOOK_SENSITIVITY = 0.0035f; // radians per pixel
		constexpr float MAX_PITCH = glm::radians(89.0f);
		constexpr float FAST_FACTOR = 3.0f;
		constexpr float MIN_SPEED = 0.1f;
		constexpr float MAX_SPEED = 1000.0f;
	}

	void EditorCamera::reset()
	{
		m_Position = glm::vec3(0.0f, 2.5f, 7.0f);
		m_Yaw = 0.0f;
		m_Pitch = glm::radians(-15.0f);
		m_Speed = 4.0f;
	}

	glm::vec3 EditorCamera::forward() const
	{
		return glm::vec3(-std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), -std::cos(m_Yaw) * std::cos(m_Pitch));
	}

	void EditorCamera::apply(float deltaSeconds, const EditorCameraInput& input)
	{
		m_Yaw -= input.Look.x * LOOK_SENSITIVITY;
		m_Pitch = std::clamp(m_Pitch - input.Look.y * LOOK_SENSITIVITY, -MAX_PITCH, MAX_PITCH);

		if (input.Scroll != 0.0f)
		{
			m_Speed = std::clamp(m_Speed * std::pow(1.15f, input.Scroll), MIN_SPEED, MAX_SPEED);
		}

		const glm::vec3 front = forward();
		const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::vec3 motion = right * input.Move.x + glm::vec3(0.0f, 1.0f, 0.0f) * input.Move.y + front * input.Move.z;
		m_Position += motion * (m_Speed * (input.Fast ? FAST_FACTOR : 1.0f) * deltaSeconds);
	}

	RenderCamera EditorCamera::get(float aspect) const
	{
		const glm::mat4 world = glm::translate(glm::mat4(1.0f), m_Position) * glm::mat4_cast(glm::angleAxis(m_Yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(m_Pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
		return make_render_camera(Camera{}, world, aspect);
	}
}
