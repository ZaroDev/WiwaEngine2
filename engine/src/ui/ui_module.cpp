#include <pch.h>
#include "ui_module.h"

#include <algorithm>
#include <unordered_set>

#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/scene/scene_extractor.h>
#include <scene/scene_module.h>

namespace wiwa
{
	void UiModule::init()
	{
		m_Assets = Engine::get().get_module<AssetManager>();
		if (m_Assets == nullptr)
		{
			return;
		}

		AssetManager* assets = m_Assets;
		m_Loader = [assets](const AssetRef& ref) { return assets->load<UiPanelData>(ref); };
		m_Fonts = [this](const AssetRef& ref) -> const FontData*
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			auto it = m_FontHandles.find(ref.resolved());
			if (it == m_FontHandles.end())
			{
				it = m_FontHandles.emplace(ref.resolved(), m_Assets->load<FontData>(ref)).first;
			}
			return it->second.get();
		};
		m_TextureSizes = [this](const AssetRef& ref) -> std::optional<glm::vec2>
		{
			if (!ref.valid())
			{
				return std::nullopt;
			}
			const AssetGuid guid = ref.resolved();
			if (auto known = m_TextureSizeCache.find(guid); known != m_TextureSizeCache.end())
			{
				return known->second;
			}
			// Only the size is kept: the pixels belong to the renderer, and the handle is dropped as soon as the size is known.
			auto it = m_TextureHandles.find(guid);
			if (it == m_TextureHandles.end())
			{
				it = m_TextureHandles.emplace(guid, m_Assets->load<TextureData>(ref)).first;
			}
			const TextureData* texture = it->second.get();
			if (texture == nullptr)
			{
				return std::nullopt;
			}
			const glm::vec2 size(static_cast<float>(texture->Width), static_cast<float>(texture->Height));
			m_TextureSizeCache[guid] = size;
			m_TextureHandles.erase(it);
			return size;
		};

		// the listener runs on whatever thread cooked the asset: it only notes the guid
		m_ReloadListener = m_Assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				m_ReloadQueue.push_back(guid);
			});
	}

	void UiModule::shutdown()
	{
		if (m_Assets != nullptr && m_ReloadListener != 0)
		{
			m_Assets->remove_reload_listener(m_ReloadListener);
			m_ReloadListener = 0;
		}
		m_Instances.clear();
		m_CanvasEntities.clear();
		m_FontHandles.clear();
		m_TextureHandles.clear();
		m_TextureSizeCache.clear();
	}

	void UiModule::reload_panel(const AssetRef& panel)
	{
		for (auto& [guid, instance] : m_Instances)
		{
			if (instance->panel_ref().resolved() == panel.resolved())
			{
				instance->reload();
			}
		}
	}

	UiPanelInstance* UiModule::instance_of(entt::entity canvas)
	{
		const auto entity = m_CanvasEntities.find(canvas);
		if (entity == m_CanvasEntities.end())
		{
			return nullptr;
		}
		const auto instance = m_Instances.find(entity->second);
		return instance == m_Instances.end() ? nullptr : instance->second.get();
	}

	UiPanelInstance* UiModule::instance_for(const Scene& scene, const EntityGuid& canvas)
	{
		if (m_Scene != &scene)
		{
			return nullptr;
		}
		const auto instance = m_Instances.find(canvas);
		return instance == m_Instances.end() ? nullptr : instance->second.get();
	}

	std::vector<UiModule::Active> UiModule::sync(Scene& scene)
	{
		if (m_Scene != &scene)
		{
			m_Instances.clear();
			m_CanvasEntities.clear();
			m_Scene = &scene;
		}

		struct Entry
		{
			i32 SortOrder;
			u32 Entity;
			Active Item;
		};
		std::vector<Entry> entries;
		std::unordered_set<EntityGuid> seen;
		std::unordered_map<entt::entity, EntityGuid> entities;

		for (const auto [entity, id, canvas] : scene.registry().view<Id, UICanvas>().each())
		{
			if (!canvas.Panel.valid())
			{
				continue;
			}

			auto it = m_Instances.find(id.Guid);
			if (it == m_Instances.end() || !(it->second->panel_ref() == canvas.Panel))
			{
				it = m_Instances.insert_or_assign(id.Guid, CreateScope<UiPanelInstance>(canvas.Panel)).first;
			}
			seen.insert(id.Guid);
			entities[entity] = id.Guid;
			entries.push_back(Entry{ canvas.SortOrder, static_cast<u32>(entt::to_integral(entity)), Active{ it->second.get(), canvas.Visible, id.Guid } });
		}

		for (auto it = m_Instances.begin(); it != m_Instances.end();)
		{
			it = seen.contains(it->first) ? std::next(it) : m_Instances.erase(it);
		}
		m_CanvasEntities = std::move(entities);

		// bottom to top; canvases of one order go by when their entity was made
		std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
			{
				return a.SortOrder != b.SortOrder ? a.SortOrder < b.SortOrder : a.Entity < b.Entity;
			});

		std::vector<Active> result;
		result.reserve(entries.size());
		for (const Entry& entry : entries)
		{
			result.push_back(entry.Item);
		}
		return result;
	}

	UiLayoutInput UiModule::make_layout_input(Scene& scene, const glm::vec2& canvasSize)
	{
		UiLayoutInput input;
		input.CanvasSize = canvasSize;

		m_Camera = UiCameraView{};
		const float aspect = canvasSize.y > 0.0f ? canvasSize.x / canvasSize.y : 1.0f;
		if (m_CameraOverride)
		{
			m_Camera.ViewProjection = m_CameraOverride->Projection * m_CameraOverride->View;
			m_Camera.Position = m_CameraOverride->Position;
			m_Camera.Valid = true;
		}
		else
		{
			for (const auto [entity, id, camera, world] : scene.registry().view<Id, Camera, WorldTransform>().each())
			{
				if (camera.Primary)
				{
					const RenderCamera render = make_render_camera(camera, world.Matrix, aspect);
					m_Camera.ViewProjection = render.Projection * render.View;
					m_Camera.Position = render.Position;
					m_Camera.Valid = true;
					break;
				}
			}
		}
		input.Camera = &m_Camera;

		input.WorldPosition = [&scene](const EntityGuid& guid) -> std::optional<glm::vec3>
		{
			const entt::entity entity = scene.find(guid);
			if (entity == entt::null)
			{
				return std::nullopt;
			}
			const WorldTransform* world = scene.registry().try_get<WorldTransform>(entity);
			return world != nullptr ? std::optional<glm::vec3>(glm::vec3(world->Matrix[3])) : std::nullopt;
		};
		input.Fonts = m_Fonts;
		input.TextureSize = m_TextureSizes;
		input.ViewTexture = [this](std::string_view name) -> u32
		{
			RenderModule* render = Engine::get().get_module<RenderModule>();
			const ViewId view = render != nullptr ? render->get_view_names().find(name) : INVALID_VIEW;
			if (view == INVALID_VIEW)
			{
				// Said once per name: a menu that names a host that is not there (yet) is not an error every frame.
				if (m_WarnedViews.insert(std::string(name)).second)
				{
					WI_LOG(Warning, "UI", "No view named '{0}' (a ViewImage widget names a ViewHost)", std::string(name));
				}
				return 0;
			}
			return view_texture_id(view);
		};
		return input;
	}

	void UiModule::update_interaction(Scene& scene, const glm::vec2& canvasSize)
	{
		const std::vector<Active> active = sync(scene);
		for (const Active& item : active)
		{
			item.Instance->events().clear();
			item.Instance->poll(m_Loader);
		}
		if (m_VideoEvents)
		{
			for (const UiVideoEvent& finished : m_VideoEvents())
			{
				for (const Active& item : active)
				{
					if (item.Canvas == finished.Canvas)
					{
						item.Instance->events().push_back(UiEvent{ UiEventKind::Finished, finished.Widget, 0.0f, finished.Skipped, finished.Failed });
					}
				}
			}
		}

		const bool hasArea = canvasSize.x > 0.0f && canvasSize.y > 0.0f;
		UiInputFrame input = m_Input != nullptr ? m_Input->poll() : UiInputFrame{};
		if (!hasArea)
		{
			return;
		}

		const UiLayoutInput layoutInput = make_layout_input(scene, canvasSize);

		// The canvas on top goes first. Once a widget holds the pointer the canvases below see no pointer; navigation goes to one canvas.
		bool pointerTaken = false;
		bool navigationTaken = false;
		for (auto it = active.rbegin(); it != active.rend(); ++it)
		{
			UiPanelInstance& instance = *it->Instance;
			if (!instance.ready() || !it->CanvasVisible || !instance.visible())
			{
				instance.interaction().reset();
				instance.states().assign(instance.data().Widgets.size(), UiInteractionState::Normal);
				continue;
			}

			UiInputFrame frame = input;
			if (pointerTaken)
			{
				frame.PointerValid = false;
				for (u32 button = 0; button < UI_POINTER_BUTTONS; button++)
				{
					frame.Down[button] = frame.Pressed[button] = frame.Released[button] = false;
				}
			}
			if (navigationTaken)
			{
				frame.NavPressed = 0;
			}
			navigationTaken = true;

			const UiLayoutResult layout = compute_layout(instance.data(), layoutInput);
			instance.interaction().update(instance.data(), layout, instance.states(), frame, instance.events());
			pointerTaken = pointerTaken || instance.interaction().hovered() != 0 || instance.interaction().captured() != 0;
		}
	}

	void UiModule::build_draw_list(Scene& scene, const glm::vec2& canvasSize, UiDrawList& out)
	{
		const std::vector<Active> active = sync(scene);
		if (!(canvasSize.x > 0.0f) || !(canvasSize.y > 0.0f))
		{
			return;
		}

		const UiLayoutInput layoutInput = make_layout_input(scene, canvasSize);
		for (const Active& item : active)
		{
			UiPanelInstance& instance = *item.Instance;
			instance.poll(m_Loader);
			if (!instance.ready() || !item.CanvasVisible || !instance.visible())
			{
				continue;
			}

			// the widgets of this canvas ask for their pictures by canvas
			UiLayoutInput canvasInput = layoutInput;
			if (m_VideoProvider)
			{
				canvasInput.Video = [this, canvas = item.Canvas](UiId widget) { return m_VideoProvider(canvas, widget); };
			}
			const UiLayoutResult layout = compute_layout(instance.data(), canvasInput);
			append_panel(out, instance.data(), instance.states(), layout, canvasInput);
		}
	}

	void UiModule::update(float)
	{
		{
			std::vector<AssetGuid> reloaded;
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				reloaded.swap(m_ReloadQueue);
			}
			for (const AssetGuid& guid : reloaded)
			{
				m_FontHandles.erase(guid);
				m_TextureHandles.erase(guid);
				m_TextureSizeCache.erase(guid);
				reload_panel(AssetRef{ guid, 0 });
			}
		}

		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (scenes == nullptr)
		{
			return;
		}
		RenderModule* render = Engine::get().get_module<RenderModule>();
		update_interaction(*scenes->get_active(), m_CanvasOverride ? *m_CanvasOverride : (render != nullptr ? render->get_canvas_size() : glm::vec2(0.0f)));
	}

	void UiModule::pre_render_update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		RenderModule* render = Engine::get().get_module<RenderModule>();
		if (scenes == nullptr || render == nullptr)
		{
			return;
		}

		UiDrawList list;
		build_draw_list(*scenes->get_active(), m_CanvasOverride ? *m_CanvasOverride : render->get_canvas_size(), list);
		render->set_ui_draw_list(std::move(list));
	}
}
