#include <pch.h>
#include "scene_render_data.h"

#include <algorithm>

#include "frustum_culler.h"

namespace wiwa
{
	namespace
	{
		constexpr u32 INITIAL_INSTANCE_CAPACITY = 1024;
		// How many frames' worth of constants the volatile buffer can hold at once.
		constexpr u32 CONSTANT_BUFFER_VERSIONS = 16;
		constexpr u32 DRAW_CONSTANTS_BYTES = 8; // instance index, material slot

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
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(1, DRAW_CONSTANTS_BYTES));
		m_FrameLayout = device->createBindingLayout(layout);

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(FrameConstants);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = CONSTANT_BUFFER_VERSIONS;
		constants.debugName = "Frame constants";
		m_ConstantBuffer = device->createBuffer(constants);

		return m_FrameLayout && m_ConstantBuffer;
	}

	void SceneRenderData::shutdown()
	{
		m_FrameBindings = nullptr;
		m_InstanceBuffer = nullptr;
		m_ConstantBuffer = nullptr;
		m_FrameLayout = nullptr;
		m_Device = nullptr;
	}

	void SceneRenderData::build(RenderScene& scene, GpuResourceCache& cache, ViewportTarget& viewport)
	{
		m_Opaque.clear();
		m_Masked.clear();
		m_Blended.clear();
		m_VisibleInstances = 0;
		m_Active = viewport.valid();
		m_Viewport = &viewport;
		m_Cache = &cache;
		m_Scene = &scene;
		m_Settings = scene.settings();
		if (!m_Active)
		{
			m_HasCamera = false;
			return;
		}

		FrameConstants constants;
		constants.SkyColor = glm::vec4(m_Settings.SkyColor * m_Settings.AmbientIntensity, 0.0f);
		constants.GroundColor = glm::vec4(m_Settings.GroundColor * m_Settings.AmbientIntensity, 0.0f);
		const float width = static_cast<float>(viewport.width());
		const float height = static_cast<float>(viewport.height());
		constants.ViewportSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);

		// The sun: the first directional light.
		for (const RenderLight& light : scene.lights())
		{
			if (light.Alive && light.Kind == LightKind::Directional)
			{
				constants.SunDirection = glm::vec4(light.Direction, 1.0f);
				constants.SunColor = glm::vec4(light.Color * light.Intensity, 0.0f);
				break;
			}
		}

		const std::optional<RenderCamera>& camera = scene.effective_camera();
		m_HasCamera = camera.has_value();
		if (camera)
		{
			constants.View = camera->View;
			constants.Projection = camera->Projection;
			constants.ViewProjection = camera->Projection * camera->View;
			constants.CameraPosition = glm::vec4(camera->Position, camera->Near);
			constants.CameraParams = glm::vec4(camera->Far, 0.0f, 0.0f, 0.0f);

			const Frustum frustum(constants.ViewProjection);
			const std::vector<RenderInstance>& instances = scene.instances();
			for (u32 slot = 0; slot < instances.size(); slot++)
			{
				const RenderInstance& instance = instances[slot];
				if (!instance.Alive)
				{
					continue;
				}

				const GpuMesh* mesh = cache.get_mesh(instance.Mesh);
				if (mesh == nullptr || !frustum.intersects(instance.World, mesh->BoundsMin, mesh->BoundsMax))
				{
					continue;
				}

				bool any = false;
				for (size_t p = 0; p < mesh->Primitives.size(); p++)
				{
					const GpuPrimitive& primitive = mesh->Primitives[p];
					if (!frustum.intersects(instance.World, primitive.BoundsMin, primitive.BoundsMax))
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

			std::sort(m_Opaque.begin(), m_Opaque.end(), draw_order);
			std::sort(m_Masked.begin(), m_Masked.end(), draw_order);
			std::sort(m_Blended.begin(), m_Blended.end(), [](const DrawItem& a, const DrawItem& b) { return a.Depth > b.Depth; });
		}
		m_Constants = constants;
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

		if (m_InstanceBuffer && m_WrittenRevision != m_Scene->revision())
		{
			m_InstanceScratch.assign(std::max<size_t>(instances.size(), 1), GpuInstance{});
			for (size_t slot = 0; slot < instances.size(); slot++)
			{
				if (instances[slot].Alive)
				{
					m_InstanceScratch[slot].World = instances[slot].World;
					m_InstanceScratch[slot].Normal = glm::transpose(glm::inverse(instances[slot].World));
				}
			}
			commandList->writeBuffer(m_InstanceBuffer, m_InstanceScratch.data(), m_InstanceScratch.size() * sizeof(GpuInstance));
			m_WrittenRevision = m_Scene->revision();
		}

		commandList->writeBuffer(m_ConstantBuffer, &m_Constants, sizeof(m_Constants));

		// The binding set follows its buffers: both can be replaced when they have to grow.
		nvrhi::IBuffer* materials = cache.material_buffer();
		if (m_FrameBindings == nullptr || m_BoundInstanceBuffer != m_InstanceBuffer.Get() || m_BoundMaterialBuffer != materials)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_InstanceBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, materials));
			set.addItem(nvrhi::BindingSetItem::PushConstants(1, DRAW_CONSTANTS_BYTES));
			m_FrameBindings = m_Device->createBindingSet(set, m_FrameLayout);
			m_BoundInstanceBuffer = m_InstanceBuffer;
			m_BoundMaterialBuffer = materials;
		}
	}
}
