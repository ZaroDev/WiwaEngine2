#pragma once

#include <vector>

#include <nvrhi/nvrhi.h>

#include "../scene/scene_render_data.h"

// What the scene passes (depth prepass, forward, shadows) have in common: the vertex layout of the meshes, pipeline creation and the
// loop that records a list of draw items.
namespace wiwa::scene_pass
{
	struct PipelineOptions
	{
		bool DoubleSided = false;    // no back-face culling
		bool DepthTest = true;
		bool DepthWrite = false;
		bool Blend = false;          // premultiplied-free alpha blending of color target 0
		bool StencilWrite = false;   // every pixel that passes writes StencilRef (only the bits of StencilMask) to the stencil
		u8 StencilRef = 0;
		u8 StencilMask = 0xFF;
		bool NoColorWrite = false;   // color target 0 is left alone (a mask pass)
		nvrhi::ComparisonFunc DepthFunc = nvrhi::ComparisonFunc::LessOrEqual;
	};

	// POSITION, NORMAL, TEXCOORD of MeshVertex, in one vertex buffer.
	nvrhi::InputLayoutHandle make_mesh_input_layout(nvrhi::IDevice* device, nvrhi::IShader* vertexShader);

	// A pipeline for the mesh vertex layout; pixelShader may be null (depth only). Counter-clockwise triangles are front faces.
	nvrhi::GraphicsPipelineHandle make_pipeline(nvrhi::IDevice* device, nvrhi::IInputLayout* inputLayout, nvrhi::IShader* vertexShader,
		nvrhi::IShader* pixelShader, const std::vector<nvrhi::IBindingLayout*>& layouts, nvrhi::IFramebuffer* framebuffer, const PipelineOptions& options);

	// Draws the items: one indexed draw each, with the state set again only when the pipeline, the mesh page or (with bindMaterial)
	// the material changes. extraBindings, if given, is the set after the material's. singleSided / doubleSided are the pipelines for the two cull modes; frameBindings is set 0.
	void draw_items(nvrhi::ICommandList* commandList, const SceneRenderData& data, nvrhi::IFramebuffer* framebuffer, const nvrhi::Viewport& viewport,
		nvrhi::IBindingSet* frameBindings, const std::vector<DrawItem>& items, nvrhi::IGraphicsPipeline* singleSided, nvrhi::IGraphicsPipeline* doubleSided,
		bool bindMaterial, nvrhi::IBindingSet* extraBindings = nullptr);
}
