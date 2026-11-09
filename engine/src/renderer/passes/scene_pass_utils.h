#pragma once

#include <unordered_map>
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

	// The pipelines of surface shaders (.wshader) for one kind of draw of a pass: the framebuffer, the layouts and the state are the
	// pass's, the shaders are the shader's variant. Made on the first draw that needs one and again when the shader is replaced.
	// A pass owns one per distinct framebuffer / state and calls configure() whenever those change (which drops the pipelines).
	class SurfacePipelines
	{
	public:
		// Nothing about a framebuffer is kept: an entry is made when it is first drawn, against the framebuffer being drawn into (all views have the same formats).
		void configure(nvrhi::IDevice* device, std::vector<nvrhi::IBindingLayout*> layouts, const PipelineOptions& options);
		// Null while the shader is not ready or when the pipeline cannot be made (logged once); the draw then falls back to the built-in one.
		nvrhi::IGraphicsPipeline* get(GpuResourceCache& cache, u32 shader, SurfaceVariantKind kind, bool doubleSided, nvrhi::IFramebuffer* framebuffer);

	private:
		struct Entry
		{
			u32 Generation = ~0u;
			nvrhi::GraphicsPipelineHandle Pipeline{};
			bool Failed = false;
		};

		nvrhi::IDevice* m_Device = nullptr;
		std::vector<nvrhi::IBindingLayout*> m_Layouts{};
		PipelineOptions m_Options{};
		std::unordered_map<u64, Entry> m_Entries{};
	};

	// How draw_items draws the items whose material has a surface shader (DrawItem::Shader != 0).
	struct SurfaceDraw
	{
		SurfacePipelines* Pipelines = nullptr;
		SurfaceVariantKind Kind = SurfaceVariantKind::Forward;       // the variant of an unmasked item
		SurfaceVariantKind MaskedKind = SurfaceVariantKind::Forward; // the variant of an alpha-masked one
		nvrhi::IBindingSet* FrameBindings = nullptr;                 // null: the pass's own
		nvrhi::IBindingSet* AoBindings = nullptr;                    // set 2 of the surface layouts (the sets are frame, material, AO, surface, skin)
		bool Caster = false;                                         // shadow casters: the push constants carry matrix | (material slot << 3)
	};

	// Which items draw_items takes by DrawItem::ReceivesDecals (the depth prepass draws the two kinds with different stencil state).
	enum class DecalFilter
	{
		All,
		Receivers,
		NonReceivers,
	};

	// Draws the items: one indexed draw each, with the state set again only when the pipeline, the mesh page or (with bindMaterial)
	// the material changes. extraBindings, if given, is the set after the material's; the skin set of the page comes last. singleSided / doubleSided are the pipelines for the two cull modes; frameBindings is set 0.
	void draw_items(nvrhi::ICommandList* commandList, const SceneRenderData& data, nvrhi::IFramebuffer* framebuffer, const nvrhi::Viewport& viewport,
		nvrhi::IBindingSet* frameBindings, const std::vector<DrawItem>& items, nvrhi::IGraphicsPipeline* singleSided, nvrhi::IGraphicsPipeline* doubleSided,
		bool bindMaterial, nvrhi::IBindingSet* extraBindings = nullptr, const SurfaceDraw* surface = nullptr, DecalFilter filter = DecalFilter::All);
}
