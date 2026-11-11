#pragma once

#include <vector>

#include <renderer/debug/debug_draw.h>
#include <renderer/render_world.h>
#include <renderer/renderer.h>
#include "render_test.h"
#include "test_meshes.h"

// Renders small scenes through the whole renderer (extraction aside: the scene mirror is filled directly) into the viewport target
// and reads the 8-bit result back, for the render checks. Pixels are RGBA, (0, 0) is the top left.
namespace wiwa
{
	class SceneTester
	{
	public:
		SceneTester(RenderCheckContext& context, u32 size = 64)
			: m_Context(context), m_Renderer(context.World.get_renderer()), m_Size(size)
		{
			m_Renderer.set_scene_in_viewport(true);
			m_Renderer.set_viewport_size(size, size);

			SceneDelta reset;
			reset.Reset = true;
			scene().apply(std::move(reset));
			scene().set_settings(SceneSettings{});
			RenderQuality quality = m_Renderer.get_quality(); // the shadow values go back to the defaults, vsync and the queue stay
			const RenderQuality defaults;
			quality.ShadowDistance = defaults.ShadowDistance;
			quality.ShadowCascades = defaults.ShadowCascades;
			quality.ShadowSplitLambda = defaults.ShadowSplitLambda;
			quality.ShadowDepthBias = defaults.ShadowDepthBias;
			quality.ShadowNormalBias = defaults.ShadowNormalBias;
			quality.SpotShadowBias = defaults.SpotShadowBias;
			quality.BloomMips = defaults.BloomMips;
			quality.AoSlices = defaults.AoSlices;
			quality.AoSteps = defaults.AoSteps;
			quality.Fxaa = defaults.Fxaa;
			quality.Taa = defaults.Taa;
			m_Renderer.set_quality(quality);
			m_Renderer.get_scene_data().set_debug_velocity(false);
			scene().set_camera_override(std::nullopt);
			scene().set_selection(std::nullopt); // the selection and its look belong to the renderer, not the scene: no check inherits another's
			scene().set_outline_style(SelectionOutlineStyle{});
			set_camera(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
		}

		RenderScene& scene() { return m_Renderer.get_scene(); }
		// The debug shapes of the frames this tester renders: sealed into every batch (shapes with a duration stay for several frames).
		DebugDraw& debug() { return m_DebugDraw; }
		// The UI of the frames this tester renders: copied into every batch (set CanvasSize to the size of the image).
		UiDrawList& ui() { return m_Ui; }
		GpuResourceCache& cache() { return m_Renderer.get_cache(); }
		Renderer& renderer() { return m_Renderer; }
		u32 size() const { return m_Size; }
		// A new viewport size (square); the next frame makes the target again.
		void set_size(u32 size)
		{
			m_Size = size;
			m_Renderer.set_viewport_size(size, size);
		}

		AssetRef add_mesh(MeshData mesh) { return cache().add_inline_mesh(std::move(mesh)); }
		AssetRef add_material(MaterialData material) { return cache().add_inline_material(std::move(material)); }
		AssetRef add_texture(TextureData texture) { return cache().add_inline_texture(std::move(texture)); }

		MaterialData color_material(glm::vec3 color, float roughness = 1.0f, float metallic = 0.0f) const
		{
			MaterialData material;
			material.BaseColorFactor = glm::vec4(color, 1.0f);
			material.RoughnessFactor = roughness;
			material.MetallicFactor = metallic;
			return material;
		}

		EntityGuid add_instance(const AssetRef& mesh, const glm::mat4& world, std::vector<AssetRef> materials = {})
		{
			const EntityGuid guid = next_guid();
			RenderInstance instance;
			instance.World = world;
			instance.Mesh = mesh;
			instance.Materials = std::move(materials);
			SceneDelta delta;
			delta.InstanceUpserts.emplace_back(guid, std::move(instance));
			scene().apply(std::move(delta));
			return guid;
		}

		// Moves an instance that exists: the same mesh and materials with another world matrix.
		void set_instance_world(EntityGuid guid, const glm::mat4& world)
		{
			const std::optional<size_t> slot = scene().find_instance(guid);
			if (!slot)
			{
				return;
			}
			RenderInstance instance;
			instance.World = world;
			instance.Mesh = scene().instances()[*slot].Mesh;
			instance.Materials = scene().instances()[*slot].Materials;
			SceneDelta delta;
			delta.InstanceUpserts.emplace_back(guid, std::move(instance));
			scene().apply(std::move(delta));
		}

		void remove_instance(EntityGuid guid)
		{
			SceneDelta delta;
			delta.InstanceRemovals.push_back(guid);
			scene().apply(std::move(delta));
		}

		EntityGuid add_light(const RenderLight& light)
		{
			const EntityGuid guid = next_guid();
			RenderLight copy = light;
			SceneDelta delta;
			delta.LightUpserts.emplace_back(guid, std::move(copy));
			scene().apply(std::move(delta));
			return guid;
		}

		EntityGuid add_decal(const RenderDecal& decal)
		{
			const EntityGuid guid = next_guid();
			RenderDecal copy = decal;
			SceneDelta delta;
			delta.DecalUpserts.emplace_back(guid, std::move(copy));
			scene().apply(std::move(delta));
			return guid;
		}

		void remove_decal(EntityGuid guid)
		{
			SceneDelta delta;
			delta.DecalRemovals.push_back(guid);
			scene().apply(std::move(delta));
		}

		RenderLight sun(glm::vec3 travelDirection, float intensity = 3.0f, bool casts = false) const
		{
			RenderLight light;
			light.Kind = LightKind::Directional;
			light.Direction = glm::normalize(travelDirection);
			light.Intensity = intensity;
			light.CastsShadows = casts;
			return light;
		}

		void set_camera(glm::vec3 position, glm::vec3 target, float fovDegrees = 60.0f, float nearPlane = 0.1f, float farPlane = 100.0f)
		{
			RenderCamera camera;
			camera.View = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
			camera.Projection = glm::perspectiveRH_ZO(glm::radians(fovDegrees), 1.0f, nearPlane, farPlane);
			camera.Position = position;
			camera.Near = nearPlane;
			camera.Far = farPlane;
			m_Camera = camera;
			scene().set_camera_override(camera);
		}

		// Where a world point lands in the image.
		glm::ivec2 to_pixel(glm::vec3 point) const
		{
			const glm::vec4 clip = m_Camera.Projection * m_Camera.View * glm::vec4(point, 1.0f);
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return glm::ivec2(static_cast<int>((ndc.x * 0.5f + 0.5f) * m_Size), static_cast<int>((1.0f - (ndc.y * 0.5f + 0.5f)) * m_Size));
		}

		// Renders the given number of frames (at least enough for what was added to load), then reads the image. False when a frame failed.
		bool render(u32 frames = 6)
		{
			const auto started = std::chrono::steady_clock::now();
			for (u32 i = 0; i < 20000; i++)
			{
				if (!frame())
				{
					return false;
				}
				if (std::chrono::steady_clock::now() - started > std::chrono::seconds(40))
				{
					break;
				}
				// An environment, and the BRDF table that comes with it, load on other threads: wait for them (a few ms per frame).
				bool environmentBusy = false;
				if (scene().settings().Source == EnvironmentSource::File)
				{
					const EnvironmentCache::Stats stats = m_Renderer.get_environments().get_stats();
					environmentBusy = (stats.Pending > 0 || !stats.BrdfReady) && stats.Failed == 0;
				}
				if (scene().settings().Source == EnvironmentSource::Atmosphere)
				{
					const EnvironmentCache& environments = m_Renderer.get_environments();
					environmentBusy = environments.atmosphere() == nullptr || environments.atmosphere_builder().building();
					if (environments.atmosphere_status().rfind("failed", 0) == 0)
					{
						environmentBusy = false;
					}
				}
				if (i + 1 >= frames && cache().get_stats().Pending == 0 && !environmentBusy)
				{
					break;
				}
				if (environmentBusy)
				{
					// a build of the atmosphere takes seconds: wait at about the pace of a display, not in a tight loop of presents
					std::this_thread::sleep_for(std::chrono::milliseconds(scene().settings().Source == EnvironmentSource::Atmosphere ? 16 : 2));
				}
			}
			return read();
		}

		// The frame time that the next frames report (a sixtieth of a second to begin with).
		void set_delta(float seconds) { m_Delta = seconds; }

		// How many frames this tester has rendered (the index of the next one).
		u64 frame_count() const { return m_Frame; }

		// One frame, and no more.
		bool frame()
		{
			RenderBatch batch;
			batch.FrameIndex = m_Frame++;
			batch.DeltaTime = m_Delta;
			batch.DeltaSeconds = m_Delta;
			batch.Width = m_Renderer.get_width();
			batch.Height = m_Renderer.get_height();
			batch.Debug = m_DebugDraw.seal(m_Delta);
			batch.Ui = m_Ui;
			return m_Renderer.render_frame(batch);
		}

		bool read()
		{
			ViewportTarget& viewport = m_Renderer.get_viewport();
			if (!viewport.valid() || viewport.width() != m_Size)
			{
				return false;
			}
			u32 width = 0;
			u32 height = 0;
			return read_texture_rgba8(m_Context.Device, viewport.ldr(), m_Pixels, width, height);
		}

		const std::vector<u8>& pixels() const { return m_Pixels; }
		glm::ivec3 pixel(int x, int y) const
		{
			const size_t offset = (static_cast<size_t>(y) * m_Size + static_cast<size_t>(x)) * 4;
			return glm::ivec3(m_Pixels[offset], m_Pixels[offset + 1], m_Pixels[offset + 2]);
		}
		glm::ivec3 pixel(glm::ivec2 at) const { return pixel(at.x, at.y); }
		static int luma(glm::ivec3 color) { return (color.r * 3 + color.g * 6 + color.b) / 10; }

		// The largest per-channel difference between two renders of the same size.
		static int max_difference(const std::vector<u8>& a, const std::vector<u8>& b)
		{
			if (a.size() != b.size())
			{
				return 255;
			}
			int worst = 0;
			for (size_t i = 0; i < a.size(); i++)
			{
				if ((i & 3) != 3)
				{
					worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
				}
			}
			return worst;
		}

	private:
		EntityGuid next_guid() { return EntityGuid{ 0x7E57, ++m_NextGuid }; }

		RenderCheckContext& m_Context;
		Renderer& m_Renderer;
		u32 m_Size;
		RenderCamera m_Camera{};
		std::vector<u8> m_Pixels{};
		u64 m_Frame = 0;
		float m_Delta = 1.0f / 60.0f;
		u64 m_NextGuid = 0;
		DebugDraw m_DebugDraw{};
		UiDrawList m_Ui{};
	};
}
