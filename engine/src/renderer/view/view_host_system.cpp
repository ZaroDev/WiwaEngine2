#include <pch.h>
#include "view_host_system.h"

#include <algorithm>

#include <animation/animation_module.h>
#include <assets/asset_manager.h>
#include <core/engine.h>

#include "../render_module.h"
#include "view_camera.h"

namespace wiwa
{
	static_assert(static_cast<u32>(ViewHostProfile::Full) == static_cast<u32>(ViewProfile::Full));
	static_assert(static_cast<u32>(ViewHostProfile::Preview) == static_cast<u32>(ViewProfile::Preview));
	static_assert(static_cast<u32>(ViewHostProfile::Thumbnail) == static_cast<u32>(ViewProfile::Thumbnail));

	namespace
	{
		constexpr i32 MAX_HOST_SIZE = 4096;

		u32 size_of(i32 pixels)
		{
			return static_cast<u32>(std::clamp(pixels, 1, MAX_HOST_SIZE));
		}

		// A host's fields that need another view when they change.
		bool needs_new_view(const ViewHost& a, const ViewHost& b)
		{
			return a.Profile != b.Profile || a.Transparent != b.Transparent || a.Continuous != b.Continuous;
		}
	}

	void ViewHostSystem::clear()
	{
		m_Hosts.clear(); // the handles destroy their views
		m_Scene.reset();
	}

	void ViewHostSystem::update(RenderModule& render, const Ref<Scene>& scene, float deltaTime)
	{
		if (scene == nullptr)
		{
			clear();
			return;
		}
		if (m_Scene.lock() != scene)
		{
			clear(); // another scene: its hosts are other entities
			m_Scene = scene;
		}

		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets == nullptr)
		{
			return;
		}

		for (auto& [guid, host] : m_Hosts)
		{
			host.Seen = false;
		}

		for (const auto [entity, id, component] : scene->registry().view<Id, ViewHost>().each())
		{
			Host& host = m_Hosts[id.Guid];
			host.Seen = true;

			const bool wantNewView = !host.View.valid() || !host.HasApplied || needs_new_view(host.Applied, component);
			if (wantNewView)
			{
				host.View = RenderView{};
				ViewDesc description;
				description.Width = size_of(component.Width);
				description.Height = size_of(component.Height);
				description.Profile = static_cast<ViewProfile>(component.Profile);
				description.TransparentBackground = component.Transparent;
				description.Mode = component.Continuous ? ViewMode::Continuous : ViewMode::OneShot;
				host.View = render.create_view(description);
				if (!host.View.valid())
				{
					host.HasApplied = false; // tried again next frame (the limit may have been lifted)
					continue;
				}
				if (host.Preview == nullptr)
				{
					host.Preview = make_preview_scene();
					host.Model = CreateScope<PreviewModel>(*assets, host.Preview);
					// the model's Animator plays in its small scene too (a portrait that waves)
					if (AnimationModule* animation = Engine::get().get_module<AnimationModule>())
					{
						animation->register_preview_scene(host.Preview);
					}
				}
				host.View.set_scene(host.Preview);
				host.HasApplied = false;
			}

			const bool changed = !host.HasApplied || !(host.Applied == component);
			if (!host.HasApplied || host.Applied.Name != component.Name)
			{
				render.get_view_names().set_name(host.View.id(), component.Name);
			}
			host.Model->set_prefab(component.Model);
			host.Model->update();

			host.Spin = std::fmod(host.Spin + component.SpinSpeed * deltaTime, 360.0f);
			if (host.Model->ready() && host.Preview->registry().valid(host.Model->root()))
			{
				host.Preview->registry().get<Transform>(host.Model->root()).Rotation = glm::angleAxis(glm::radians(host.Spin), glm::vec3(0.0f, 1.0f, 0.0f));
			}

			const u32 width = size_of(component.Width);
			const u32 height = size_of(component.Height);
			host.View.resize(width, height);
			host.View.set_camera(make_orbit_camera(component.Target, component.Distance, component.Yaw, component.Pitch, component.Fov,
				static_cast<float>(width) / static_cast<float>(height)));

			// A view that is drawn on request is asked when something it shows may have changed.
			const bool modelReady = host.Model->ready();
			if (!component.Continuous && (changed || modelReady != host.ModelWasReady || component.SpinSpeed != 0.0f))
			{
				host.View.request_render();
			}
			host.ModelWasReady = modelReady;
			host.Applied = component;
			host.HasApplied = true;
		}

		for (auto it = m_Hosts.begin(); it != m_Hosts.end();)
		{
			it = it->second.Seen ? std::next(it) : m_Hosts.erase(it);
		}
	}
}
