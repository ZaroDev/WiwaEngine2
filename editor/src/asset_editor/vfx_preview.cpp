#include <editor_common.h>
#include "vfx_preview.h"

#include <algorithm>
#include <cmath>

#include <core/engine.h>
#include <renderer/particles/particle_pool.h>
#include <renderer/render_module.h>
#include <renderer/view/view_camera.h>
#include <scene/components.h>
#include <scene/particle_module.h>
#include <scene/particle_runtime.h>
#include <scene/preview_scene.h>

namespace wiwa
{
	namespace
	{
		constexpr float FOV = 40.0f;

		ParticleModule* particle_module() { return Engine::get().get_module<ParticleModule>(); }
	}

	VfxPreview::~VfxPreview()
	{
		close();
	}

	bool VfxPreview::open(const AssetRef& effect)
	{
		close();
		ParticleModule* module = particle_module();
		if (module == nullptr || !effect.valid())
		{
			return false;
		}
		m_Effect = effect;
		m_Scene = make_preview_scene();
		module->register_preview_scene(m_Scene);

		m_Entity = m_Scene->create("Effect");
		ParticleSystem system;
		system.Effect = effect;
		system.PlayOnStart = true;
		m_Scene->registry().emplace<ParticleSystem>(m_Entity, system);
		m_Scene->update_world_transforms();
		ParticleRuntime& runtime = particle_runtime_of(m_Scene->registry(), m_Entity);
		runtime.Preview = true;
		apply_system_settings();
		m_Stats = Stats{};
		return true;
	}

	void VfxPreview::close()
	{
		// the view goes before the scene it shows
		m_View = RenderView{};
		if (ParticleModule* module = particle_module())
		{
			if (m_Scene != nullptr)
			{
				module->unregister_preview_scene(m_Scene);
			}
			if (m_Effect.valid())
			{
				module->clear_preview_effect(m_Effect);
			}
		}
		m_Scene.reset();
		m_Entity = entt::null;
		m_Effect = {};
	}

	void VfxPreview::push_effect(const ParticleEffectData& data)
	{
		if (ParticleModule* module = particle_module(); module != nullptr && m_Effect.valid())
		{
			module->set_preview_effect(m_Effect, data);
		}
	}

	void VfxPreview::apply_system_settings()
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(m_Entity))
		{
			return;
		}
		ParticleSystem& system = m_Scene->registry().get<ParticleSystem>(m_Entity);
		system.TimeScale = m_TimeScale;
		system.Looping = m_Looping;
	}

	void VfxPreview::restart()
	{
		if (m_Scene != nullptr && m_Scene->registry().valid(m_Entity))
		{
			particle_runtime_of(m_Scene->registry(), m_Entity).restart();
		}
	}

	void VfxPreview::set_playing(bool playing)
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(m_Entity))
		{
			return;
		}
		ParticleRuntime& runtime = particle_runtime_of(m_Scene->registry(), m_Entity);
		if (playing && runtime.Finished)
		{
			restart();
		}
		runtime.Playing = playing;
	}

	bool VfxPreview::playing() const
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(m_Entity))
		{
			return false;
		}
		const ParticleRuntime* runtime = m_Scene->registry().try_get<ParticleRuntime>(m_Entity);
		return runtime != nullptr && runtime->Playing;
	}

	float VfxPreview::play_time() const
	{
		if (m_Scene == nullptr || !m_Scene->registry().valid(m_Entity))
		{
			return 0.0f;
		}
		const ParticleRuntime* runtime = m_Scene->registry().try_get<ParticleRuntime>(m_Entity);
		return runtime != nullptr ? runtime->Time : 0.0f;
	}

	void VfxPreview::set_time_scale(float scale)
	{
		m_TimeScale = std::isfinite(scale) ? std::clamp(scale, 0.0f, 4.0f) : 1.0f;
		apply_system_settings();
	}

	void VfxPreview::set_looping(bool looping)
	{
		m_Looping = looping;
		apply_system_settings();
	}

	namespace
	{
		// The mean of a value over the particles (a Range: its middle; a Curve: the mean of its table).
		float mean_of(const ParticleValue& value)
		{
			switch (value.Kind)
			{
			case ParticleValue::Mode::Range:
				return (value.A + value.B) * 0.5f;
			case ParticleValue::Mode::Curve:
			{
				float sum = 0.0f;
				for (const float sample : value.Samples)
				{
					sum += sample;
				}
				return sum / static_cast<float>(value.Samples.size());
			}
			case ParticleValue::Mode::Constant:
			default:
				return value.A;
			}
		}

		// How many particles an emitter has alive `time` seconds into the system's play: the emission over the last lifetime (the window of the emitter's
		// duration, or all of it when it loops) plus the bursts that fell in that stretch.
		float alive_estimate(const ParticleEmitterData& emitter, float time, bool systemLooping)
		{
			const float local = time - emitter.StartDelay;
			if (local < 0.0f || emitter.Duration <= 0.0f)
			{
				return 0.0f;
			}
			const bool loop = emitter.Looping && systemLooping;
			const float life = std::max(mean_of(emitter.Lifetime), 0.0f);
			const float from = std::max(local - life, 0.0f);

			float emitting = 0.0f; // seconds of emission inside [from, local]
			if (loop)
			{
				emitting = local - from;
			}
			else
			{
				emitting = std::max(std::min(local, emitter.Duration) - std::min(from, emitter.Duration), 0.0f);
			}
			float count = std::max(mean_of(emitter.Rate), 0.0f) * emitting;

			for (const ParticleBurst& burst : emitter.Bursts)
			{
				if (loop)
				{
					// the burst of every cycle whose time falls in [from, local]
					const int firstCycle = static_cast<int>(std::ceil((from - burst.Time) / emitter.Duration));
					const int lastCycle = static_cast<int>(std::floor((local - burst.Time) / emitter.Duration));
					count += static_cast<float>(burst.Count) * static_cast<float>(std::max(lastCycle - std::max(firstCycle, 0) + 1, 0));
				}
				else if (burst.Time >= from && burst.Time <= local && burst.Time <= emitter.Duration)
				{
					count += static_cast<float>(burst.Count);
				}
			}
			return std::min(count, static_cast<float>(emitter.MaxParticles)); // a full ring overwrites its oldest
		}
	}

	void VfxPreview::update_stats(const ParticleEffectData& effect)
	{
		m_Stats = Stats{};
		m_Stats.Capacity = ParticlePool::DEFAULT_VIEW_CAPACITY;
		float alive = 0.0f;
		const float time = play_time();
		for (const ParticleEmitterData& emitter : effect.Emitters)
		{
			m_Stats.Allocated += emitter.MaxParticles;
			alive += alive_estimate(emitter, time, m_Looping);
		}
		m_Stats.Alive = static_cast<u32>(alive + 0.5f);
	}
	void VfxPreview::draw_overlay(const ParticleEffectData& effect, int selected, ImVec2 imageMin, ImVec2 size)
	{
		if (!m_ShowShape || selected < 0 || static_cast<size_t>(selected) >= effect.Emitters.size())
		{
			return;
		}
		const ParticleEmitterData& emitter = effect.Emitters[static_cast<size_t>(selected)];
		ImDrawList* list = ImGui::GetWindowDrawList();
		list->PushClipRect(imageMin, ImVec2(imageMin.x + size.x, imageMin.y + size.y), true);
		const ImU32 color = IM_COL32(255, 210, 90, 220);

		const auto project = [&](const glm::vec3& point, ImVec2& out)
		{
			const glm::vec4 clip = m_ViewProjection * glm::vec4(point, 1.0f);
			if (clip.w <= 1e-4f)
			{
				return false;
			}
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			out = ImVec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * size.x, imageMin.y + (0.5f - ndc.y * 0.5f) * size.y);
			return true;
		};
		const auto line = [&](const glm::vec3& a, const glm::vec3& b)
		{
			ImVec2 pa;
			ImVec2 pb;
			if (project(a, pa) && project(b, pb))
			{
				list->AddLine(pa, pb, color, 1.5f);
			}
		};
		const auto circle = [&](const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius)
		{
			constexpr int SEGMENTS = 32;
			glm::vec3 previous = center + u * radius;
			for (int i = 1; i <= SEGMENTS; i++)
			{
				const float angle = static_cast<float>(i) / SEGMENTS * glm::two_pi<float>();
				const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
				line(previous, next);
				previous = next;
			}
		};

		const glm::vec3 x(1.0f, 0.0f, 0.0f);
		const glm::vec3 y(0.0f, 1.0f, 0.0f);
		const glm::vec3 z(0.0f, 0.0f, 1.0f);
		switch (emitter.Shape)
		{
		case ParticleShape::Point:
			line(-x * 0.15f, x * 0.15f);
			line(-y * 0.15f, y * 0.15f);
			line(-z * 0.15f, z * 0.15f);
			break;
		case ParticleShape::Sphere:
		{
			const float radius = std::max(emitter.ShapeSize.x, 0.0f);
			circle(glm::vec3(0.0f), x, y, radius);
			circle(glm::vec3(0.0f), y, z, radius);
			circle(glm::vec3(0.0f), x, z, radius);
			break;
		}
		case ParticleShape::Cone:
		{
			const float radius = std::max(emitter.ShapeSize.x, 0.0f);
			const float angle = glm::radians(std::clamp(emitter.ConeAngle, 0.0f, 89.0f));
			const float height = 1.0f; // how far the cone is drawn
			const float topRadius = radius + std::tan(angle) * height;
			circle(glm::vec3(0.0f), x, z, radius);
			circle(y * height, x, z, topRadius);
			line(x * radius, x * topRadius + y * height);
			line(-x * radius, -x * topRadius + y * height);
			line(z * radius, z * topRadius + y * height);
			line(-z * radius, -z * topRadius + y * height);
			break;
		}
		case ParticleShape::Box:
		{
			const glm::vec3 h = glm::max(emitter.ShapeSize, glm::vec3(0.0f)) * 0.5f;
			for (int i = 0; i < 8; i++)
			{
				const glm::vec3 a((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
				for (int axis = 0; axis < 3; axis++)
				{
					if ((i & (1 << axis)) == 0)
					{
						glm::vec3 b = a;
						b[axis] = h[axis]; // the other end of this edge
						line(a, b);
					}
				}
			}
			break;
		}
		}
		list->PopClipRect();
	}

	void VfxPreview::draw(ImVec2 size, const ParticleEffectData& effect, int selected)
	{
		update_stats(effect);

		// transport
		if (ImGui::Button(playing() ? "Pause" : "Play"))
		{
			set_playing(!playing());
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart"))
		{
			restart();
		}
		ImGui::SameLine();
		bool looping = m_Looping;
		if (ImGui::Checkbox("Loop", &looping))
		{
			set_looping(looping);
		}
		ImGui::SameLine();
		ImGui::Checkbox("Shape", &m_ShowShape);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		float scale = m_TimeScale;
		if (ImGui::SliderFloat("Speed", &scale, 0.0f, 3.0f, "%.2fx"))
		{
			set_time_scale(scale);
		}
		ImGui::Text("Time %.2f s    Particles ~%u    Pool %u of %u (rings)", play_time(), m_Stats.Alive, m_Stats.Allocated, m_Stats.Capacity);
		if (m_Stats.Allocated > m_Stats.Capacity)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "(over: the rings share a pool of %u in this preview)", m_Stats.Capacity);
		}

		size.x = std::max(size.x, 16.0f);
		size.y = std::max(ImGui::GetContentRegionAvail().y, 16.0f); // what is left under the controls

		if (!m_View.valid())
		{
			RenderModule* render = Engine::get().get_module<RenderModule>();
			if (render == nullptr || !render->is_ready() || m_Scene == nullptr)
			{
				ImGui::Dummy(size);
				return;
			}
			ViewDesc description;
			description.Width = static_cast<u32>(size.x);
			description.Height = static_cast<u32>(size.y);
			description.Profile = ViewProfile::Preview;
			description.Mode = ViewMode::Continuous;
			m_View = render->create_view(description);
			if (!m_View.valid())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Could not make a view (too many are live)");
				ImGui::Dummy(size);
				return;
			}
			m_View.set_scene(m_Scene);
		}

		m_View.resize(static_cast<u32>(size.x), static_cast<u32>(size.y));
		const RenderCamera camera = make_orbit_camera(m_Target, m_Distance, m_Yaw, m_Pitch, FOV, size.x / size.y);
		m_View.set_camera(camera);
		m_ViewProjection = camera.Projection * camera.View;

		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		ImGui::Image(static_cast<ImTextureID>(m_View.imgui_texture_id()), size);
		if (ImGui::IsItemHovered() && !ImGui::IsAnyItemActive())
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				m_Yaw -= delta.x * 0.4f;
				m_Pitch = std::clamp(m_Pitch + delta.y * 0.4f, -85.0f, 85.0f);
			}
			m_Distance = std::clamp(m_Distance * (1.0f - ImGui::GetIO().MouseWheel * 0.1f), 0.5f, 100.0f);
		}
		if (m_View.failed())
		{
			ImGui::GetWindowDrawList()->AddText(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f), IM_COL32(255, 100, 100, 255), "The view could not make its target");
		}
		draw_overlay(effect, selected, imageMin, size);
	}
}
