#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

#include <assets/asset_manager.h>
#include <renderer/debug/debug_draw.h>
#include <scene/scene.h>

#include "gizmo_settings.h"

namespace wiwa
{
	// The camera of the viewport, for sizing the markers so that they keep their size on screen. A view that cannot be used (no viewport
	// height, a NaN matrix) gives markers of 1 unit.
	struct SceneGizmoView
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		float ViewportHeight = 0.0f;
	};

	// The overlays of the scene, drawn through DebugDraw every frame: a grid on the ground, the world axes, a marker and a pick icon for every light and camera, the arrow of a
	// directional light, the frustum of a camera, a marker and pick icon for every audio emitter, and for the selected entity the range sphere or cone of a
	// light, the range of an audio emitter and the world bounds of a mesh. Each category is switched on or off in GizmoSettings. Main thread.
	class SceneGizmos
	{
	public:
		using Bounds = std::pair<glm::vec3, glm::vec3>;
		// The local bounds (min, max) of a mesh asset, or none while it has not loaded.
		using BoundsLookup = std::function<std::optional<Bounds>(const AssetRef& mesh)>;

		// Looks the bounds up through the asset manager (loading the mesh the first time it is asked for).
		explicit SceneGizmos(AssetManager& assets) : m_Assets(&assets) {}
		explicit SceneGizmos(BoundsLookup lookup) : m_Lookup(std::move(lookup)) {}

		// The distance an audio emitter is heard from (its event's attenuation times its scale), 0 when unknown: the range sphere of the selected emitter.
		using AudioRadiusLookup = std::function<float(const AudioEmitter& emitter)>;
		void set_audio_radius(AudioRadiusLookup lookup) { m_AudioRadius = std::move(lookup); }

		void draw(DebugDraw& draw, Scene& scene, entt::entity selected, const GizmoSettings& settings, const SceneGizmoView& view);

	private:
		std::optional<Bounds> bounds_of(const AssetRef& mesh);

		AssetManager* m_Assets = nullptr;
		BoundsLookup m_Lookup{};
		AudioRadiusLookup m_AudioRadius{};
		std::unordered_map<AssetGuid, Handle<MeshData>> m_Meshes{};
	};
}
