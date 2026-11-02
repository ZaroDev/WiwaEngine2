#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <core/module.h>
#include <renderer/scene/render_scene.h>
#include <scene/scene.h>

#include "ui_draw_list.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_panel_instance.h"

namespace wiwa
{
	// Runs the UI of the active scene. Every UICanvas gets a UiPanelInstance. Each frame:
	//   update():            the input goes to the canvases (the one on top first), widgets react, their events are kept on the instance;
	//                        this runs before the scripts, so a script sees this frame's clicks
	//   pre_render_update(): after the scripts moved things, the layout is made again, the draw list built and handed to the renderer
	// The two halves are public for tests and tools.
	class UiModule : public IModule
	{
	public:
		using PanelLoader = UiPanelInstance::Loader;
		using FontProvider = std::function<const FontData*(const AssetRef&)>;
		using TextureSizeProvider = std::function<std::optional<glm::vec2>(const AssetRef&)>;

		UiModule() = default;
		virtual ~UiModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;
		virtual void pre_render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const override { return "UiModule"; }
		virtual int get_priority() const override { return -10; } // after the scene and the renderer, before the scripts (0)

		// Where the pointer and navigation come from. Null (the default) is no input at all. Not owned.
		void set_input_source(IUiInputSource* source) { m_Input = source; }
		// An input source that code fills in (tests, tools, UI.input in Lua). It is only read while it is the input source in use.
		ScriptedInputSource& scripted_input() { return m_Scripted; }
		IUiInputSource* input_source() const { return m_Input; }

		// Draw anchors with this camera instead of the scene's primary one (the editor's fly camera); nothing goes back to the scene's.
		void set_camera_override(const std::optional<RenderCamera>& camera) { m_CameraOverride = camera; }

		// How panels, fonts and texture sizes are found. The defaults ask the AssetManager; tests and tools put their own.
		void set_panel_loader(PanelLoader loader) { m_Loader = std::move(loader); }
		void set_font_provider(FontProvider provider) { m_Fonts = std::move(provider); }
		void set_texture_size_provider(TextureSizeProvider provider) { m_TextureSizes = std::move(provider); }

		// The panel asset was cooked again: its canvases ask for it again.
		void reload_panel(const AssetRef& panel);

		// The instance of the canvas on this entity of the scene that was last looked at, or null.
		UiPanelInstance* instance_of(entt::entity canvas);

		// The instance of the canvas on this entity of the scene the UI module looked at last, or null (also when the scene is another one).
		UiPanelInstance* instance_for(const Scene& scene, const EntityGuid& canvas);
		size_t instance_count() const { return m_Instances.size(); }

		// The two halves of a frame. canvasSize is the size of what the scene is drawn into, in pixels.
		void update_interaction(Scene& scene, const glm::vec2& canvasSize);
		void build_draw_list(Scene& scene, const glm::vec2& canvasSize, UiDrawList& out);

	private:
		struct Active
		{
			UiPanelInstance* Instance = nullptr;
			bool CanvasVisible = true;
		};

		std::vector<Active> sync(Scene& scene);
		UiLayoutInput make_layout_input(Scene& scene, const glm::vec2& canvasSize);

		AssetManager* m_Assets = nullptr;
		u32 m_ReloadListener = 0;
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadQueue{};

		IUiInputSource* m_Input = nullptr;
		ScriptedInputSource m_Scripted{};
		std::optional<RenderCamera> m_CameraOverride{};
		PanelLoader m_Loader{};
		FontProvider m_Fonts{};
		TextureSizeProvider m_TextureSizes{};
		std::unordered_map<AssetGuid, Handle<FontData>> m_FontHandles{};
		std::unordered_map<AssetGuid, Handle<TextureData>> m_TextureHandles{};

		const Scene* m_Scene = nullptr; // the scene the instances belong to (identity only)
		std::unordered_map<EntityGuid, Scope<UiPanelInstance>> m_Instances{};
		std::unordered_map<entt::entity, EntityGuid> m_CanvasEntities{};
		UiCameraView m_Camera{};
	};
}
