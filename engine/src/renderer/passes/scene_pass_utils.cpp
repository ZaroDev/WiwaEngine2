#include <pch.h>
#include "scene_pass_utils.h"

namespace wiwa::scene_pass
{
	nvrhi::InputLayoutHandle make_mesh_input_layout(nvrhi::IDevice* device, nvrhi::IShader* vertexShader)
	{
		nvrhi::VertexAttributeDesc attributes[3];
		attributes[0].name = "POSITION";
		attributes[0].format = nvrhi::Format::RGB32_FLOAT;
		attributes[0].offset = offsetof(MeshVertex, Position);
		attributes[0].elementStride = sizeof(MeshVertex);
		attributes[1].name = "NORMAL";
		attributes[1].format = nvrhi::Format::RGB32_FLOAT;
		attributes[1].offset = offsetof(MeshVertex, Normal);
		attributes[1].elementStride = sizeof(MeshVertex);
		attributes[2].name = "TEXCOORD";
		attributes[2].format = nvrhi::Format::RG32_FLOAT;
		attributes[2].offset = offsetof(MeshVertex, Uv);
		attributes[2].elementStride = sizeof(MeshVertex);
		return device->createInputLayout(attributes, 3, vertexShader);
	}

	nvrhi::GraphicsPipelineHandle make_pipeline(nvrhi::IDevice* device, nvrhi::IInputLayout* inputLayout, nvrhi::IShader* vertexShader,
		nvrhi::IShader* pixelShader, const std::vector<nvrhi::IBindingLayout*>& layouts, nvrhi::IFramebuffer* framebuffer, const PipelineOptions& options)
	{
		nvrhi::GraphicsPipelineDesc description;
		description.primType = nvrhi::PrimitiveType::TriangleList;
		description.inputLayout = inputLayout;
		description.VS = vertexShader;
		description.PS = pixelShader;
		for (nvrhi::IBindingLayout* layout : layouts)
		{
			description.addBindingLayout(layout);
		}

		nvrhi::RenderState& state = description.renderState;
		state.depthStencilState.depthTestEnable = options.DepthTest;
		state.depthStencilState.depthWriteEnable = options.DepthWrite;
		state.depthStencilState.depthFunc = options.DepthFunc;
		state.depthStencilState.stencilEnable = options.StencilWrite;
		if (options.StencilWrite)
		{
			state.depthStencilState.stencilReadMask = 0xFF;
			state.depthStencilState.stencilWriteMask = options.StencilMask;
			state.depthStencilState.stencilRefValue = options.StencilRef;
			state.depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Always;
			state.depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;
			state.depthStencilState.backFaceStencil = state.depthStencilState.frontFaceStencil;
		}
		state.rasterState.cullMode = options.DoubleSided ? nvrhi::RasterCullMode::None : nvrhi::RasterCullMode::Back;
		state.rasterState.frontCounterClockwise = true;

		if (options.Blend)
		{
			nvrhi::BlendState::RenderTarget& blend = state.blendState.targets[0];
			blend.blendEnable = true;
			blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
			blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOp = nvrhi::BlendOp::Add;
			blend.srcBlendAlpha = nvrhi::BlendFactor::One;
			blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOpAlpha = nvrhi::BlendOp::Add;
		}

		if (options.NoColorWrite)
		{
			state.blendState.targets[0].colorWriteMask = static_cast<nvrhi::ColorMask>(0);
		}

		return device->createGraphicsPipeline(description, framebuffer);
	}

	void draw_items(nvrhi::ICommandList* commandList, const SceneRenderData& data, nvrhi::IFramebuffer* framebuffer, const nvrhi::Viewport& viewport,
		nvrhi::IBindingSet* frameBindings, const std::vector<DrawItem>& items, nvrhi::IGraphicsPipeline* singleSided, nvrhi::IGraphicsPipeline* doubleSided,
		bool bindMaterial, nvrhi::IBindingSet* extraBindings)
	{
		if (items.empty() || frameBindings == nullptr)
		{
			return;
		}

		GpuResourceCache& cache = *data.cache();

		struct DrawConstants
		{
			u32 InstanceIndex;
			u32 MaterialSlot;
			u32 BaseVertex; // D3D does not count it in SV_VertexID, which skin vertices are indexed by
		};

		nvrhi::IGraphicsPipeline* currentPipeline = nullptr;
		u32 currentPage = ~0u;
		u32 currentMaterial = ~0u;

		for (const DrawItem& item : items)
		{
			nvrhi::IGraphicsPipeline* pipeline = item.DoubleSided ? doubleSided : singleSided;
			if (pipeline == nullptr)
			{
				continue;
			}

			if (pipeline != currentPipeline || item.Page != currentPage || (bindMaterial && item.Material != currentMaterial))
			{
				nvrhi::GraphicsState state;
				state.pipeline = pipeline;
				state.framebuffer = framebuffer;
				state.viewport.addViewportAndScissorRect(viewport);
				state.addBindingSet(frameBindings);
				if (bindMaterial)
				{
					state.addBindingSet(cache.get_material_bindings(item.Material));
				}
				if (extraBindings != nullptr)
				{
					state.addBindingSet(extraBindings);
				}
				state.addBindingSet(data.skin_bindings(item.Page)); // always the last set

				nvrhi::VertexBufferBinding vertexBinding;
				vertexBinding.buffer = cache.vertex_buffer(item.Page);
				vertexBinding.slot = 0;
				vertexBinding.offset = 0;
				state.addVertexBuffer(vertexBinding);
				state.indexBuffer.buffer = cache.index_buffer(item.Page);
				state.indexBuffer.format = nvrhi::Format::R32_UINT;
				state.indexBuffer.offset = 0;
				commandList->setGraphicsState(state);

				currentPipeline = pipeline;
				currentPage = item.Page;
				currentMaterial = item.Material;
			}

			const DrawConstants constants{ item.Instance, item.Material, static_cast<u32>(item.BaseVertex) };
			commandList->setPushConstants(&constants, sizeof(constants));

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = item.IndexCount; // index count for indexed draws
			arguments.instanceCount = 1;
			arguments.startIndexLocation = item.FirstIndex;
			arguments.startVertexLocation = static_cast<u32>(item.BaseVertex); // reinterpreted as a signed offset by NVRHI
			commandList->drawIndexed(arguments);
		}
	}
}
