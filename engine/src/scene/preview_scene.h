#pragma once

#include <assets/asset_data.h>
#include <assets/asset_manager.h>
#include <assets/handle.h>

#include "scene.h"

// Small scenes made to look at one thing: asset previews, thumbnails, 3D widgets in the UI (views.md). They are ordinary Scenes, drawn by a RenderView.
namespace wiwa
{
	struct PreviewSceneOptions
	{
		glm::vec3 Background{ 0.18f };   // the solid background (a view with a transparent background ignores it)
		bool Ground = false;             // a ground plane under the model (needs the engine's plane mesh: not yet, ignored)
		float ExposureEv = 0.0f;
	};

	// A neutral studio: a key light that casts shadows, a fill and a rim light, a solid background and manual exposure. No camera: the view brings
	// its own (view_camera.h).
	Ref<Scene> make_preview_scene(const PreviewSceneOptions& options = {});

	// Shows a prefab in a preview scene: set_prefab() starts loading it, update() (main thread, every frame) creates its entities under one root
	// entity once it is ready. A new prefab replaces the model.
	class PreviewModel
	{
	public:
		PreviewModel(AssetManager& assets, Ref<Scene> scene) : m_Assets(assets), m_Scene(std::move(scene)) {}
		~PreviewModel();

		PreviewModel(const PreviewModel&) = delete;
		PreviewModel& operator=(const PreviewModel&) = delete;

		void set_prefab(const AssetRef& prefab);
		void update();

		bool ready() const { return m_Root != entt::null; }
		bool failed() const { return m_Prefab.valid() && m_Handle.state() == LoadState::Failed; }
		// The root entity of the model, or entt::null while it is loading (or when there is no prefab).
		entt::entity root() const { return m_Root; }
		const AssetRef& prefab() const { return m_Prefab; }

	private:
		void clear();

		AssetManager& m_Assets;
		Ref<Scene> m_Scene;
		AssetRef m_Prefab{};
		Handle<PrefabData> m_Handle{};
		entt::entity m_Root = entt::null;
	};
}
