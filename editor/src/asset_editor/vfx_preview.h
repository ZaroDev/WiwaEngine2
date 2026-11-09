#pragma once

#include <imgui.h>

#include <assets/particle_effect.h>
#include <renderer/view/render_view.h>
#include <scene/scene.h>

namespace wiwa
{
	// The preview of the VFX editor: a preview scene with one entity that has a ParticleSystem playing the effect, drawn by a RenderView with an orbit camera
	// (drag to turn, wheel to zoom), the transport controls, the shape of the selected emitter drawn over the image, and the numbers of the pool.
	// The effect it plays is whatever the editor last pushed (set_preview_effect), so unsaved edits show at once.
	class VfxPreview
	{
	public:
		// What the pool holds, as far as the main thread can tell (the GPU keeps its own count): the particles spawned in the last lifetime of each emitter,
		// capped by its MaxParticles. The numbers are estimates and the panel says so.
		struct Stats
		{
			u32 Alive = 0;       // estimated
			u32 Allocated = 0;   // the sum of the MaxParticles of the emitters: the rings the effect takes in the pool
			u32 Capacity = 0;    // what a view's pool holds
		};

		VfxPreview() = default;
		~VfxPreview();
		VfxPreview(const VfxPreview&) = delete;
		VfxPreview& operator=(const VfxPreview&) = delete;

		// Starts the preview. The effect plays under a guid of its own (push_effect gives it the data), so the asset on disk, the Scene view and the thumbnails
		// keep showing what is saved. False when the engine cannot (no particle module).
		bool open();
		void close();
		bool is_open() const { return m_Scene != nullptr; }

		// The effect as edited, to play instead of the file's.
		void push_effect(const ParticleEffectData& data);

		void restart();
		void set_playing(bool playing);
		bool playing() const;
		float time_scale() const { return m_TimeScale; }
		void set_time_scale(float scale);
		bool looping() const { return m_Looping; }
		void set_looping(bool looping);

		// The controls, the image and the overlay. selected: the emitter whose shape is drawn (-1 none).
		void draw(ImVec2 size, const ParticleEffectData& effect, int selected);

		// For the smoke test
		const Stats& stats() const { return m_Stats; }
		RenderView& view() { return m_View; }
		const Ref<Scene>& scene() const { return m_Scene; }
		entt::entity entity() const { return m_Entity; }
		float play_time() const;

	private:
		void update_stats(const ParticleEffectData& effect);
		void apply_system_settings();
		void draw_overlay(const ParticleEffectData& effect, int selected, ImVec2 imageMin, ImVec2 size);

		AssetRef m_Effect{};
		Ref<Scene> m_Scene{};
		entt::entity m_Entity = entt::null;
		RenderView m_View{};

		float m_TimeScale = 1.0f;
		bool m_Looping = true;
		bool m_ShowShape = true;

		float m_Yaw = 25.0f;
		float m_Pitch = 20.0f;
		float m_Distance = 4.0f;
		glm::vec3 m_Target{ 0.0f, 0.8f, 0.0f };
		glm::mat4 m_ViewProjection{ 1.0f };

		Stats m_Stats{};
		std::string m_Notice{};
	};
}
