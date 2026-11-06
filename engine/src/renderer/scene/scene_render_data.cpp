#include <pch.h>
#include "scene_render_data.h"

#include <algorithm>
#include <cmath>

#include "../ibl/atmosphere_environment.h"
#include "../passes/taa_math.h"
#include "cascade_math.h"
#include "frustum_culler.h"

namespace wiwa
{
	namespace
	{
		constexpr u32 INITIAL_INSTANCE_CAPACITY = 1024;
		// How many frames' worth of constants the volatile buffer can hold at once.
		constexpr u32 CONSTANT_BUFFER_VERSIONS = 16;
		constexpr u32 DRAW_CONSTANTS_BYTES = 12; // instance index, material slot, base vertex of the draw

		nvrhi::VulkanBindingOffsets make_zero_binding_offsets()
		{
			nvrhi::VulkanBindingOffsets offsets;
			offsets.shaderResource = 0;
			offsets.sampler = 0;
			offsets.constantBuffer = 0;
			offsets.unorderedAccess = 0;
			return offsets;
		}

		// Sort key: single sided first, then by page, then by material.
		bool draw_order(const DrawItem& a, const DrawItem& b)
		{
			if (a.DoubleSided != b.DoubleSided)
			{
				return !a.DoubleSided;
			}
			if (a.Page != b.Page)
			{
				return a.Page < b.Page;
			}
			return a.Material < b.Material;
		}
	}

	nvrhi::TextureHandle SceneRenderData::make_shadow_texture(u32 width, u32 height, u32 slices, const char* name)
	{
		nvrhi::TextureDesc description;
		description.width = width;
		description.height = height;
		description.arraySize = slices;
		description.dimension = slices > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
		description.format = nvrhi::Format::D32;
		description.isRenderTarget = true;
		description.isShaderResource = true;
		description.isTypeless = true;
		description.initialState = nvrhi::ResourceStates::DepthWrite;
		description.keepInitialState = true; // NVRHI needs a known state to start from; the frame ends with the map back in it
		description.useClearValue = true;
		description.clearValue = nvrhi::Color(1.0f, 0.0f, 0.0f, 0.0f);
		description.debugName = name;
		return m_Device->createTexture(description);
	}

	bool SceneRenderData::initialize(nvrhi::IDevice* device)
	{
		m_Device = device;

		// Slots match scene_common.hlsli: b0 constants, SRV 1 instances, SRV 2 materials, push constants 1.
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = make_zero_binding_offsets();
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(6));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(7));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(10));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(8));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(9));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(11));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(1, DRAW_CONSTANTS_BYTES));
		m_FrameLayout = device->createBindingLayout(layout);

		// Slot t15 of set 2 on Vulkan (see ao_layout()).
		nvrhi::BindingLayoutDesc aoLayout;
		aoLayout.visibility = nvrhi::ShaderType::Pixel;
		aoLayout.bindingOffsets = make_zero_binding_offsets();
		aoLayout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(15));
		m_AoLayout = device->createBindingLayout(aoLayout);

		// t22 and t23 of the skin set (scene_common.hlsli): vertex shaders only
		nvrhi::BindingLayoutDesc skinLayout;
		skinLayout.visibility = nvrhi::ShaderType::Vertex;
		skinLayout.bindingOffsets = make_zero_binding_offsets();
		skinLayout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(22));
		skinLayout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(23));
		m_SkinLayout = device->createBindingLayout(skinLayout);

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(FrameConstants);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = CONSTANT_BUFFER_VERSIONS;
		constants.debugName = "Frame constants";
		m_ConstantBuffer = device->createBuffer(constants);

		auto make_structured = [&](u64 elements, u32 stride, bool unorderedAccess, const char* name)
		{
			nvrhi::BufferDesc description;
			description.byteSize = elements * stride;
			description.structStride = stride;
			description.canHaveUAVs = unorderedAccess;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = name;
			return device->createBuffer(description);
		};
		m_DummyPalette = make_structured(3, sizeof(glm::vec4), false, "Palette stand-in");
		m_DummySkin = make_structured(1, 32, false, "Skin vertices stand-in");
		m_LightBuffer = make_structured(MAX_LIGHTS, sizeof(GpuLight), false, "Scene lights");
		m_ClusterCounts = make_structured(CLUSTER_COUNT, sizeof(u32), true, "Cluster light counts");
		m_ClusterIndices = make_structured(static_cast<u64>(CLUSTER_COUNT) * MAX_LIGHTS_PER_CLUSTER, sizeof(u32), true, "Cluster light indices");

		// Shadow maps are depth textures compared against by a hardware sampler. The real ones are made when something casts shadows;
		// until then 1x1 stand-ins keep the bindings valid.
		const nvrhi::SamplerDesc samplerDescription = nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setReductionType(nvrhi::SamplerReductionType::Comparison);
		m_ShadowSampler = device->createSampler(samplerDescription);
		m_DummyCascadeMap = make_shadow_texture(1, 1, MAX_CASCADES, "Cascade stand-in");
		m_DummySpotAtlas = make_shadow_texture(1, 1, 1, "Spot atlas stand-in");

		// The environment: a trilinear sampler, and stand-ins (written with zeros before their first use) for when there is none.
		m_EnvironmentSampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		nvrhi::TextureDesc dummyCube;
		dummyCube.width = 1;
		dummyCube.height = 1;
		dummyCube.arraySize = 6;
		dummyCube.dimension = nvrhi::TextureDimension::TextureCube;
		dummyCube.format = nvrhi::Format::RGBA16_FLOAT;
		dummyCube.isShaderResource = true;
		dummyCube.initialState = nvrhi::ResourceStates::ShaderResource;
		dummyCube.keepInitialState = true;
		dummyCube.debugName = "Environment stand-in";
		m_DummyCube = device->createTexture(dummyCube);
		nvrhi::TextureDesc dummyBrdf;
		dummyBrdf.width = 1;
		dummyBrdf.height = 1;
		dummyBrdf.format = nvrhi::Format::RG16_FLOAT;
		dummyBrdf.isShaderResource = true;
		dummyBrdf.initialState = nvrhi::ResourceStates::ShaderResource;
		dummyBrdf.keepInitialState = true;
		dummyBrdf.debugName = "BRDF table stand-in";
		m_DummyBrdf = device->createTexture(dummyBrdf);
		nvrhi::TextureDesc dummyAo;
		dummyAo.width = 1;
		dummyAo.height = 1;
		dummyAo.format = nvrhi::Format::R8_UNORM;
		dummyAo.isShaderResource = true;
		dummyAo.initialState = nvrhi::ResourceStates::ShaderResource;
		dummyAo.keepInitialState = true;
		dummyAo.debugName = "AO stand-in";
		m_DummyAo = device->createTexture(dummyAo);

		return m_FrameLayout && m_ConstantBuffer && m_LightBuffer && m_ClusterCounts && m_ClusterIndices && m_ShadowSampler && m_EnvironmentSampler && m_DummyCube && m_DummyBrdf && m_DummyAo && m_AoLayout && m_DummyCascadeMap && m_DummySpotAtlas;
	}

	void SceneRenderData::shutdown()
	{
		m_FrameBindings = nullptr;
		m_CascadeMap = nullptr;
		m_SpotAtlas = nullptr;
		m_DummyCascadeMap = nullptr;
		m_DummySpotAtlas = nullptr;
		m_ShadowSampler = nullptr;
		m_EnvironmentSampler = nullptr;
		m_DummyCube = nullptr;
		m_DummyBrdf = nullptr;
		m_DummyAo = nullptr;
		m_AoBindings = nullptr;
		m_AoLayout = nullptr;
		m_FrameEnvironment = nullptr;
		m_LightBuffer = nullptr;
		m_ClusterCounts = nullptr;
		m_ClusterIndices = nullptr;
		m_InstanceBuffer = nullptr;
		m_ConstantBuffer = nullptr;
		m_FrameLayout = nullptr;
		m_SkinSets.clear();
		m_DefaultSkinSet = SkinSet{};
		m_DummyPalette = nullptr;
		m_DummySkin = nullptr;
		m_PaletteBuffer = nullptr;
		m_SkinLayout = nullptr;
		m_Device = nullptr;
	}

	void SceneRenderData::build(RenderScene& scene, GpuResourceCache& cache, ViewportTarget& viewport)
	{
		m_Opaque.clear();
		m_Masked.clear();
		m_Blended.clear();
		m_Lights.clear();
		m_ShadowViews.clear();
		m_NeedCascades = false;
		m_NeedSpotAtlas = false;
		m_VisibleInstances = 0;
		m_DroppedLights = 0;
		m_Active = viewport.valid();
		m_Viewport = &viewport;
		m_Cache = &cache;
		m_Scene = &scene;
		m_Settings = scene.settings();
		m_AoActive = false;
		if (!m_Active)
		{
			m_HasCamera = false;
			m_TemporalActive = false;
			m_HistoryValid = false;
			m_PreviousKnown = false;
			return;
		}

		FrameConstants constants;
		constants.SkyColor = glm::vec4(m_Settings.HemisphereSky * m_Settings.AmbientIntensity, 0.0f);
		constants.GroundColor = glm::vec4(m_Settings.HemisphereGround * m_Settings.AmbientIntensity, 0.0f);
		const float width = static_cast<float>(viewport.width());
		const float height = static_cast<float>(viewport.height());
		constants.ViewportSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);

		// The environment of the scene, when it is ready; until then (and without one) the hemisphere ambient lights the scene.
		m_FrameEnvironment = nullptr;
		m_SkyKind = SkyKind::None;
		bool useAtmosphere = false;
		if (m_Environments != nullptr && m_Settings.Source == EnvironmentSource::File)
		{
			m_FrameEnvironment = m_Environments->get(m_Settings.EnvironmentAsset);
			m_SkyKind = m_FrameEnvironment != nullptr ? SkyKind::Environment : SkyKind::None;
		}
		else if (m_Environments != nullptr && m_Settings.Source == EnvironmentSource::Atmosphere)
		{
			useAtmosphere = true;
			m_SkyKind = SkyKind::Atmosphere; // drawn analytically at once; the ambient light follows when its build is uploaded
		}
		if (m_FrameEnvironment != nullptr)
		{
			for (int i = 0; i < 9; i++)
			{
				constants.EnvSH[i] = m_FrameEnvironment->SH[i];
			}
			constants.EnvParams = glm::vec4(1.0f, static_cast<float>(m_FrameEnvironment->MipCount - 1), m_Settings.Brightness * m_Settings.AmbientIntensity,
				m_Environments->brdf_lut() != nullptr ? 1.0f : 0.0f);
		}
		const float rotation = glm::radians(m_Settings.RotationDegrees);
		constants.EnvRotation = glm::vec4(std::cos(rotation), std::sin(rotation), m_Settings.Brightness, 0.0f);

		// The sun is the first directional light; the point and spot lights go to the cluster grid.
		bool haveSun = false;
		const RenderLight* sunLight = nullptr;
		std::vector<const RenderLight*> sources; // parallel to m_Lights
		for (const RenderLight& light : scene.lights())
		{
			if (!light.Alive)
			{
				continue;
			}

			if (light.Kind == LightKind::Directional)
			{
				if (!haveSun)
				{
					constants.SunDirection = glm::vec4(light.Direction, 1.0f);
					constants.SunColor = glm::vec4(light.Color * light.Intensity, 0.0f);
					sunLight = &light;
					haveSun = true;
				}
				continue;
			}

			if (m_Lights.size() >= MAX_LIGHTS)
			{
				m_DroppedLights++;
				continue;
			}

			GpuLight gpu;
			gpu.Position = light.Position;
			gpu.Range = std::max(light.Range, 0.0f);
			gpu.Color = light.Color;
			gpu.Intensity = light.Intensity;
			gpu.Direction = light.Direction;
			gpu.InnerCos = std::cos(glm::radians(std::clamp(light.InnerAngle, 0.0f, 89.0f)));
			gpu.OuterCos = std::cos(glm::radians(std::clamp(light.OuterAngle, 0.0f, 89.5f)));
			gpu.Type = light.Kind == LightKind::Spot ? 1u : 0u;
			m_Lights.push_back(gpu);
			sources.push_back(&light);
		}

		if (m_DroppedLights > 0)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - m_LastLightWarning > std::chrono::seconds(1))
			{
				m_LastLightWarning = now;
				WI_LOG(Warning, "Renderer", "{0} lights over the limit of {1} are not used", m_DroppedLights, MAX_LIGHTS);
			}
		}
		constants.ClusterParams.w = m_ClusterCulling ? 0.0f : 1.0f;
		if (useAtmosphere)
		{
			const AtmosphereDescription atmosphere = make_atmosphere_description(m_Settings.Atmosphere, sunLight != nullptr ? -sunLight->Direction : glm::vec3(0.0f));
			m_Environments->set_atmosphere_wanted(&atmosphere); // built and uploaded by the next update()
			m_FrameEnvironment = m_Environments->atmosphere();
			constants.AtmosphereA = glm::vec4(atmosphere.Sky.RayleighScale, atmosphere.Sky.MieScale, atmosphere.Sky.OzoneScale, atmosphere.Sky.MieAnisotropy);
			constants.AtmosphereB = glm::vec4(atmosphere.Sky.MultipleScattering, atmosphere.Sky.ObserverAltitude, 24.0f, 8.0f);
			constants.AtmosphereGround = glm::vec4(atmosphere.Sky.GroundAlbedo, atmosphere.DiscBoost);
			constants.AtmosphereSun = glm::vec4(atmosphere.SunDirection, 0.0f);
			constants.AtmosphereIrradiance = glm::vec4(atmosphere.SunIrradiance, atmosphere.Brightness);
			if (m_FrameEnvironment != nullptr)
			{
				for (int i = 0; i < 9; i++)
				{
					constants.EnvSH[i] = m_FrameEnvironment->SH[i];
				}
				constants.EnvParams = glm::vec4(1.0f, static_cast<float>(m_FrameEnvironment->MipCount - 1), m_Settings.Brightness * m_Settings.AmbientIntensity,
					m_Environments->brdf_lut() != nullptr ? 1.0f : 0.0f);
			}
		}
		else if (m_Environments != nullptr)
		{
			m_Environments->set_atmosphere_wanted(nullptr);
		}
		constants.LightInfo = glm::uvec4(static_cast<u32>(m_Lights.size()), 0, 0, 0);

		const std::vector<RenderInstance>& allInstances = scene.instances();
		m_Meshes.assign(allInstances.size(), nullptr);
		for (size_t slot = 0; slot < allInstances.size(); slot++)
		{
			if (allInstances[slot].Alive)
			{
				m_Meshes[slot] = cache.get_mesh(allInstances[slot].Mesh);
			}
		}

		const std::optional<RenderCamera>& camera = scene.effective_camera();
		m_HasCamera = camera.has_value();
		m_TemporalActive = false;
		m_HistoryValid = false;
		if (!camera)
		{
			m_PreviousKnown = false; // a frame without a camera breaks the history
		}
		if (camera)
		{
			constants.View = camera->View;
			// With TAA the projection that rasterises is shifted by a sub-pixel jitter. Culling, picking, debug shapes and the velocity
			// use the unjittered matrices; the depth buffer, the sky and the AO see the jittered ones.
			const glm::mat4 unjittered = camera->Projection * camera->View;
			glm::mat4 projection = camera->Projection;
			glm::vec2 jitter(0.0f);
			m_TemporalActive = m_Quality.Taa;
			if (m_TemporalActive)
			{
				jitter = taa_jitter(m_FrameIndex);
				projection = apply_jitter(projection, jitter, viewport.width(), viewport.height());
			}
			constants.Projection = projection;
			constants.ViewProjection = projection * camera->View;
			constants.InverseViewProjection = glm::inverse(constants.ViewProjection);
			constants.UnjitteredViewProjection = unjittered;

			// The previous frame's camera is a history only when nothing between the two frames makes it a different view.
			const bool cut = m_PreviousKnown && glm::distance(camera->Position, m_PreviousCameraPosition) > CAMERA_CUT_DISTANCE;
			m_HistoryValid = m_PreviousKnown && !m_HistoryResetRequested && !cut && m_PreviousViewportGeneration == viewport.generation()
				&& m_PreviousSceneResets == scene.reset_count() && m_PreviousOverride == scene.has_camera_override();
			if (!m_HistoryValid)
			{
				m_HistoryResetCount++;
			}
			constants.PreviousViewProjection = m_HistoryValid ? m_PreviousViewProjection : unjittered;
			constants.JitterInfo = glm::vec4(jitter, m_TemporalActive ? 1.0f : 0.0f, m_HistoryValid ? 1.0f : 0.0f);
			m_PreviousKnown = true;
			m_HistoryResetRequested = false;
			m_PreviousViewProjection = unjittered;
			m_PreviousCameraPosition = camera->Position;
			m_PreviousViewportGeneration = viewport.generation();
			m_PreviousSceneResets = scene.reset_count();
			m_PreviousOverride = scene.has_camera_override();
			constants.CameraPosition = glm::vec4(camera->Position, camera->Near);
			constants.CameraParams = glm::vec4(camera->Far, 0.0f, 0.0f, 0.0f);

			// Slices are exponential in view depth between near and far: slice = log2(depth) * scale + bias.
			const float depthRatio = std::log2(std::max(camera->Far / camera->Near, 1.0001f));
			const float sliceScale = static_cast<float>(CLUSTERS_Z) / depthRatio;
			constants.ClusterParams = glm::vec4(sliceScale, -std::log2(camera->Near) * sliceScale, camera->Projection[3][3] == 1.0f ? 1.0f : 0.0f, m_ClusterCulling ? 0.0f : 1.0f);

			const Frustum frustum(constants.UnjitteredViewProjection);
			const std::vector<RenderInstance>& instances = scene.instances();
			for (u32 slot = 0; slot < instances.size(); slot++)
			{
				const RenderInstance& instance = instances[slot];
				if (!instance.Alive)
				{
					continue;
				}

				const GpuMesh* mesh = m_Meshes[slot];
				if (mesh == nullptr)
				{
					continue;
				}
				// One primitive: its box is the mesh's box, tested once.
				const bool single = mesh->Primitives.size() == 1;
				const glm::vec3 padding(instance.BoundsPadding);
				if (!single && !visible(frustum, instance.World, mesh->BoundsMin - padding, mesh->BoundsMax + padding))
				{
					continue;
				}

				bool any = false;
				for (size_t p = 0; p < mesh->Primitives.size(); p++)
				{
					const GpuPrimitive& primitive = mesh->Primitives[p];
					if (!visible(frustum, instance.World, primitive.BoundsMin - padding, primitive.BoundsMax + padding))
					{
						continue;
					}

					const bool overridden = p < instance.Materials.size() && instance.Materials[p].valid();
					const u32 material = overridden ? cache.get_material_slot(instance.Materials[p]) : primitive.MaterialSlot;
					const GpuMaterialRecord& record = cache.material_record(material);

					DrawItem item;
					item.Instance = slot;
					item.Material = material;
					item.Page = mesh->Page;
					item.FirstIndex = primitive.FirstIndex;
					item.IndexCount = primitive.IndexCount;
					item.BaseVertex = primitive.BaseVertex;
					item.DoubleSided = (record.Flags & MATERIAL_DOUBLE_SIDED_BIT) != 0;

					if (record.Flags & MATERIAL_ALPHA_BLEND_BIT)
					{
						const glm::vec3 center = glm::vec3(instance.World * glm::vec4((primitive.BoundsMin + primitive.BoundsMax) * 0.5f, 1.0f));
						item.Depth = -(camera->View * glm::vec4(center, 1.0f)).z;
						m_Blended.push_back(item);
					}
					else if (record.Flags & MATERIAL_ALPHA_MASK_BIT)
					{
						m_Masked.push_back(item);
					}
					else
					{
						m_Opaque.push_back(item);
					}
					any = true;
				}
				m_VisibleInstances += any ? 1 : 0;
			}

			// Shadows: the sun's cascades fitted to the camera, and up to four spot lights with a tile of the atlas each.
			if (sunLight != nullptr && sunLight->CastsShadows)
			{
				const u32 cascades = std::clamp(m_Quality.ShadowCascades, 1u, MAX_CASCADES);
				const CascadeSet set = compute_cascades(*camera, sunLight->Direction, cascades, m_Quality.ShadowDistance, m_Quality.ShadowSplitLambda);
				constants.ShadowParams = glm::vec4(1.0f, static_cast<float>(cascades), m_Quality.ShadowDepthBias, m_Quality.ShadowNormalBias);
				for (u32 i = 0; i < MAX_CASCADES; i++)
				{
					constants.ShadowMatrices[i] = set.ViewProjection[i];
					constants.CascadeSplits[i] = set.SplitDepths[i];
					constants.CascadeTexels[i] = set.TexelWorldSize[i];
				}
				for (u32 i = 0; i < cascades; i++)
				{
					ShadowView view;
					view.ViewProjection = set.ViewProjection[i];
					view.MatrixIndex = i;
					view.Slice = i;
					collect_casters(view.ViewProjection, i, view.Items);
					m_ShadowViews.push_back(std::move(view));
				}
				m_NeedCascades = true;
			}

			constants.ShadowParams2 = glm::vec4(m_Quality.SpotShadowBias, 0.0f, 0.0f, 0.0f);
			if (constants.ShadowParams.w == 0.0f)
			{
				constants.ShadowParams.w = m_Quality.ShadowNormalBias; // the spot lights use it too
			}
			u32 spotShadows = 0;
			for (size_t i = 0; i < m_Lights.size() && spotShadows < MAX_SPOT_SHADOWS; i++)
			{
				if (m_Lights[i].Type != 1u || !sources[i]->CastsShadows)
				{
					continue;
				}

				float tanHalf = 1.0f;
				const u32 tile = spotShadows++;
				const glm::mat4 matrix = compute_spot_shadow_matrix(*sources[i], tanHalf);
				constants.ShadowMatrices[MAX_CASCADES + tile] = matrix;
				constants.SpotShadowRects[tile] = glm::vec4(static_cast<float>((tile % 4) * SPOT_TILE_SIZE) / SPOT_ATLAS_SIZE,
					static_cast<float>((tile / 4) * SPOT_TILE_SIZE) / SPOT_ATLAS_SIZE, static_cast<float>(SPOT_TILE_SIZE) / SPOT_ATLAS_SIZE,
					static_cast<float>(SPOT_TILE_SIZE) / SPOT_ATLAS_SIZE);
				constants.SpotShadowInfo[tile] = glm::vec4(tanHalf, 0.0f, 0.0f, 0.0f);
				m_Lights[i].ShadowIndex = tile + 1;

				ShadowView view;
				view.ViewProjection = matrix;
				view.MatrixIndex = MAX_CASCADES + tile;
				view.Atlas = true;
				view.Slice = tile;
				collect_casters(matrix, view.MatrixIndex, view.Items);
				m_ShadowViews.push_back(std::move(view));
				m_NeedSpotAtlas = true;
			}

			std::sort(m_Opaque.begin(), m_Opaque.end(), draw_order);
			std::sort(m_Masked.begin(), m_Masked.end(), draw_order);
			std::sort(m_Blended.begin(), m_Blended.end(), [](const DrawItem& a, const DrawItem& b) { return a.Depth > b.Depth; });
		}
		m_Constants = constants;
	}

	// The draws of everything that can cast a shadow into the view: every instance whose bounds touch it, blended materials excepted.
	bool SceneRenderData::visible(const Frustum& frustum, const glm::mat4& world, const glm::vec3& boundsMin, const glm::vec3& boundsMax) const
	{
		return !m_Culling || frustum.intersects(world, boundsMin, boundsMax);
	}

	void SceneRenderData::collect_casters(const glm::mat4& viewProjection, u32 matrixIndex, std::vector<DrawItem>& items)
	{
		const Frustum frustum(viewProjection);
		const std::vector<RenderInstance>& instances = m_Scene->instances();
		for (u32 slot = 0; slot < instances.size(); slot++)
		{
			const RenderInstance& instance = instances[slot];
			if (!instance.Alive)
			{
				continue;
			}

			const GpuMesh* mesh = m_Meshes[slot];
			const glm::vec3 padding(instance.BoundsPadding);
			if (mesh == nullptr || (mesh->Primitives.size() != 1 && !visible(frustum, instance.World, mesh->BoundsMin - padding, mesh->BoundsMax + padding)))
			{
				continue;
			}

			for (size_t p = 0; p < mesh->Primitives.size(); p++)
			{
				const GpuPrimitive& primitive = mesh->Primitives[p];
				if (!visible(frustum, instance.World, primitive.BoundsMin - padding, primitive.BoundsMax + padding))
				{
					continue;
				}

				const bool overridden = p < instance.Materials.size() && instance.Materials[p].valid();
				const u32 material = overridden ? m_Cache->get_material_slot(instance.Materials[p]) : primitive.MaterialSlot;
				if (m_Cache->material_record(material).Flags & MATERIAL_ALPHA_BLEND_BIT)
				{
					continue;
				}

				DrawItem item;
				item.Instance = slot;
				item.Material = matrixIndex; // the shadow shader reads this as the matrix index
				item.Page = mesh->Page;
				item.FirstIndex = primitive.FirstIndex;
				item.IndexCount = primitive.IndexCount;
				item.BaseVertex = primitive.BaseVertex;
				item.DoubleSided = true;
				items.push_back(item);
			}
		}
		std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) { return a.Page < b.Page; });
	}

	void SceneRenderData::write(nvrhi::ICommandList* commandList, GpuResourceCache& cache)
	{
		if (!m_Active)
		{
			return;
		}

		// The instances, when the scene changed (or the buffer is new).
		const std::vector<RenderInstance>& instances = m_Scene->instances();
		const u32 needed = static_cast<u32>(std::max<size_t>(instances.size(), 1));
		if (m_InstanceBuffer == nullptr || needed > m_InstanceCapacity)
		{
			m_InstanceCapacity = std::max(INITIAL_INSTANCE_CAPACITY, needed + needed / 2);
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_InstanceCapacity) * sizeof(GpuInstance);
			description.structStride = sizeof(GpuInstance);
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = "Scene instances";
			m_InstanceBuffer = m_Device->createBuffer(description);
			m_WrittenRevision = ~0ull;
		}

		// Skinned instances are written every frame: their palettes are (and the rows they sit at follow from the instances present).
		bool anySkinned = false;
		for (size_t slot = 0; slot < instances.size() && !anySkinned; slot++)
		{
			anySkinned = instances[slot].Alive && !instances[slot].Palette.empty();
		}
		if (m_InstanceBuffer && (m_WrittenRevision != m_Scene->revision() || m_Scene->previous_stale() || anySkinned || m_HadSkinned))
		{
			m_InstanceScratch.assign(std::max<size_t>(instances.size(), 1), GpuInstance{});
			for (size_t slot = 0; slot < instances.size(); slot++)
			{
				if (instances[slot].Alive)
				{
					m_InstanceScratch[slot].World = instances[slot].World;
					m_InstanceScratch[slot].Normal = glm::transpose(glm::inverse(instances[slot].World));
					m_InstanceScratch[slot].PrevWorld = instances[slot].PrevWorld;
				}
			}
			const bool packed = pack_palettes();
			commandList->writeBuffer(m_InstanceBuffer, m_InstanceScratch.data(), m_InstanceScratch.size() * sizeof(GpuInstance));
			if (packed)
			{
				commandList->writeBuffer(m_PaletteBuffer, m_PaletteScratch.data(), m_PaletteScratch.size() * sizeof(glm::vec4));
			}
			m_HadSkinned = packed;
			m_WrittenRevision = m_Scene->revision();
		}

		// The shadow maps, once something needs them; the stand-ins are given a value before they are first read.
		if (m_NeedCascades && m_CascadeMap == nullptr)
		{
			m_CascadeMap = make_shadow_texture(CASCADE_MAP_SIZE, CASCADE_MAP_SIZE, MAX_CASCADES, "Sun shadow cascades");
			m_ShadowGeneration++;
		}
		if (m_NeedSpotAtlas && m_SpotAtlas == nullptr)
		{
			m_SpotAtlas = make_shadow_texture(SPOT_ATLAS_SIZE, SPOT_ATLAS_SIZE, 1, "Spot shadow atlas");
			m_ShadowGeneration++;
		}
		if (!m_DummiesCleared)
		{
			commandList->clearDepthStencilTexture(m_DummyCascadeMap, nvrhi::AllSubresources, true, 1.0f, false, 0);
			commandList->clearDepthStencilTexture(m_DummySpotAtlas, nvrhi::AllSubresources, true, 1.0f, false, 0);
			m_DummiesCleared = true;
		}

		if (!m_EnvironmentDummiesWritten)
		{
			const u16 zeros[8] = {};
			for (u32 face = 0; face < 6; face++)
			{
				commandList->writeTexture(m_DummyCube, face, 0, zeros, 8);
			}
			commandList->writeTexture(m_DummyBrdf, 0, 0, zeros, 4);
			const u8 white = 255;
			commandList->writeTexture(m_DummyAo, 0, 0, &white, 1);
			m_EnvironmentDummiesWritten = true;
		}

		commandList->writeBuffer(m_ConstantBuffer, &m_Constants, sizeof(m_Constants));
		if (!m_Lights.empty())
		{
			commandList->writeBuffer(m_LightBuffer, m_Lights.data(), m_Lights.size() * sizeof(GpuLight));
		}

		// The binding set follows its buffers: both can be replaced when they have to grow.
		nvrhi::IBuffer* materials = cache.material_buffer();
		nvrhi::ITexture* cascades = m_CascadeMap ? m_CascadeMap.Get() : m_DummyCascadeMap.Get();
		nvrhi::ITexture* atlas = m_SpotAtlas ? m_SpotAtlas.Get() : m_DummySpotAtlas.Get();
		nvrhi::ITexture* cube = m_FrameEnvironment != nullptr ? m_FrameEnvironment->Cube.Get() : m_DummyCube.Get();
		nvrhi::ITexture* brdf = (m_Environments != nullptr && m_Environments->brdf_lut() != nullptr) ? m_Environments->brdf_lut() : m_DummyBrdf.Get();
		m_AoActive = m_Settings.Ao.Enabled && m_HasCamera && m_Viewport != nullptr && m_Constants.ClusterParams.z < 0.5f; // not for an orthographic camera
		nvrhi::ITexture* ao = m_AoActive ? m_Viewport->ao() : m_DummyAo.Get();
		if (m_AoBindings == nullptr || m_BoundAo != ao)
		{
			nvrhi::BindingSetDesc aoSet;
			aoSet.addItem(nvrhi::BindingSetItem::Texture_SRV(15, ao));
			m_AoBindings = m_Device->createBindingSet(aoSet, m_AoLayout);
			m_BoundAo = ao;
		}
		if (m_FrameBindings == nullptr || m_BoundInstanceBuffer != m_InstanceBuffer.Get() || m_BoundMaterialBuffer != materials
			|| m_BoundCascades != cascades || m_BoundAtlas != atlas || m_BoundCube != cube || m_BoundBrdf != brdf)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_InstanceBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, materials));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_LightBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_ClusterCounts));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_ClusterIndices));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(6, cascades));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(7, atlas));
			set.addItem(nvrhi::BindingSetItem::Sampler(10, m_ShadowSampler));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(8, cube));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(9, brdf));
			set.addItem(nvrhi::BindingSetItem::Sampler(11, m_EnvironmentSampler));
			set.addItem(nvrhi::BindingSetItem::PushConstants(1, DRAW_CONSTANTS_BYTES));
			m_FrameBindings = m_Device->createBindingSet(set, m_FrameLayout);
			m_BoundInstanceBuffer = m_InstanceBuffer;
			m_BoundMaterialBuffer = materials;
			m_BoundCascades = cascades;
			m_BoundAtlas = atlas;
			m_BoundCube = cube;
			m_BoundBrdf = brdf;
		}

		update_skin_sets(cache);
	}

	bool SceneRenderData::pack_palettes()
	{
		m_PaletteScratch.clear();
		const std::vector<RenderInstance>& instances = m_Scene->instances();
		for (size_t slot = 0; slot < instances.size(); slot++)
		{
			const RenderInstance& instance = instances[slot];
			const GpuMesh* mesh = slot < m_Meshes.size() ? m_Meshes[slot] : nullptr;
			// a palette that does not reach every joint the mesh names would be read past its end: the mesh is drawn as it is
			if (!instance.Alive || instance.Palette.empty() || mesh == nullptr || !mesh->Skinned || instance.Palette.size() < static_cast<size_t>(mesh->JointCount) * 3)
			{
				continue;
			}

			m_InstanceScratch[slot].Skin.x = static_cast<u32>(m_PaletteScratch.size());
			m_PaletteScratch.insert(m_PaletteScratch.end(), instance.Palette.begin(), instance.Palette.end());
			m_InstanceScratch[slot].Skin.y = static_cast<u32>(m_PaletteScratch.size());
			const std::vector<glm::vec4>& previous = instance.PrevPalette.size() == instance.Palette.size() ? instance.PrevPalette : instance.Palette;
			m_PaletteScratch.insert(m_PaletteScratch.end(), previous.begin(), previous.end());
		}

		if (m_PaletteScratch.empty())
		{
			return false;
		}
		const u32 needed = static_cast<u32>(m_PaletteScratch.size());
		if (m_PaletteBuffer == nullptr || needed > m_PaletteCapacity)
		{
			m_PaletteCapacity = std::max<u32>(needed + needed / 2, 3 * 256 * 4);
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_PaletteCapacity) * sizeof(glm::vec4);
			description.structStride = sizeof(glm::vec4);
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = "Joint palettes";
			m_PaletteBuffer = m_Device->createBuffer(description);
		}
		return m_PaletteBuffer != nullptr;
	}

	// One set per mesh page: the palettes of this frame and the skin vertices of the page (stand-ins where there are none).
	void SceneRenderData::update_skin_sets(GpuResourceCache& cache)
	{
		nvrhi::IBuffer* palettes = m_PaletteBuffer != nullptr ? m_PaletteBuffer.Get() : m_DummyPalette.Get();
		auto refresh = [&](SkinSet& set, nvrhi::IBuffer* skin)
		{
			if (set.Set == nullptr || set.Palette != palettes || set.Skin != skin)
			{
				nvrhi::BindingSetDesc description;
				description.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(22, palettes));
				description.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(23, skin));
				set.Set = m_Device->createBindingSet(description, m_SkinLayout);
				set.Palette = palettes;
				set.Skin = skin;
			}
		};

		refresh(m_DefaultSkinSet, m_DummySkin);
		m_SkinSets.resize(cache.page_count());
		for (u32 page = 0; page < m_SkinSets.size(); page++)
		{
			nvrhi::IBuffer* skin = cache.skin_buffer(page);
			refresh(m_SkinSets[page], skin != nullptr ? skin : m_DummySkin.Get());
		}
	}

	nvrhi::IBindingSet* SceneRenderData::skin_bindings(u32 page) const
	{
		return page < m_SkinSets.size() && m_SkinSets[page].Set != nullptr ? m_SkinSets[page].Set.Get() : m_DefaultSkinSet.Set.Get();
	}
}
