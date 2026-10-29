#pragma once

#include <unordered_map>
#include <vector>

#include <scene/scene.h>

#include "render_scene.h"

namespace wiwa
{
	// The RenderCamera for a Camera component at a world transform; aspect is width / height of what it renders into. Input that
	// cannot make a camera (near >= far, an aspect of 0, a NaN in the transform, ...) is replaced by sane values; degenerate, when
	// given, says whether that happened.
	RenderCamera make_render_camera(const Camera& camera, const glm::mat4& world, float aspect, bool* degenerate = nullptr);

	// Main thread. Compares the scene's MeshRenderers, Lights and primary Camera with what it sent last time and returns what
	// changed, ready to be applied to the render thread's RenderScene. Nothing here touches the render thread.
	class SceneExtractor
	{
	public:
		// The first call, and any call with a scene other than the previous one, gives Reset = true and everything.
		SceneDelta extract(const Ref<Scene>& scene, float aspect);

	private:
		struct SentInstance
		{
			glm::mat4 World{ 1.0f };
			AssetRef Mesh{};
			std::vector<AssetRef> Materials{};
			u64 Seen = 0;
		};

		struct SentLight
		{
			RenderLight Light{};
			u64 Seen = 0;
		};

		std::weak_ptr<Scene> m_Scene{};
		std::unordered_map<EntityGuid, SentInstance> m_Instances{};
		std::unordered_map<EntityGuid, SentLight> m_Lights{};

		bool m_HasCamera = false;
		EntityGuid m_CameraId{};
		RenderCamera m_Camera{};
		float m_CameraAspect = 0.0f;
		Camera m_CameraComponent{};
		bool m_WarnedDegenerate = false;

		u64 m_Pass = 0;
	};
}
