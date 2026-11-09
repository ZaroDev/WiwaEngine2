#pragma once

#include <unordered_map>

#include <core/common.h>
#include <scene/components.h>
#include <scene/entity_guid.h>
#include <scene/preview_scene.h>

#include "render_view.h"

namespace wiwa
{
	class RenderModule;

	// Keeps a RenderView, a preview scene and a model for every ViewHost of the scene in use (views.md). Main thread, every frame from
	// RenderModule::render_update: it makes the views of new hosts, follows what a host's fields say (model, camera, size, profile, spin),
	// names the view so that a ViewImage widget finds it, and lets go of the views of hosts that are gone. A different scene starts over.
	class ViewHostSystem
	{
	public:
		void update(RenderModule& render, const Ref<Scene>& scene, float deltaTime);
		// Destroys every view (before the module goes).
		void clear();

	private:
		struct Host
		{
			RenderView View{};
			Ref<Scene> Preview{};
			Scope<PreviewModel> Model{};
			ViewHost Applied{};
			bool HasApplied = false;
			bool ModelWasReady = false;
			float Spin = 0.0f; // degrees turned so far
			bool Seen = false;
		};

		std::unordered_map<EntityGuid, Host> m_Hosts{};
		std::weak_ptr<Scene> m_Scene{};
	};
}
