#pragma once

#include "rhi_common.h"
#include "binding.h"

namespace wiwa
{
	enum class VertexStepRate : uint8_t
	{
		PER_VERTEX, PER_INSTANCE
	};

	struct VertexBufferLayout
	{
		uint32_t Binding = 0;
		uint32_t Stride = 0;
		VertexStepRate StepRate = VertexStepRate::PER_VERTEX;
	};

	struct VertextAttribute
	{
		uint32_t Location = 0;
		uint32_t Binding = 0;
		uint32_t Offset = 0;
		Format Format = Format::UNKNOWN;
	};

	struct VertexInputDescription
	{
		std::vector<VertexBufferLayout> Buffers;
		std::vector<VertexAttribute> Attributes;
	};

	enum class PrimitiveTopology : uint8_t
	{
		POINT_LIST, LINE_LIST, LINE_STRIP, TRIANGLE_LIST, TRIANGLE_STRIP, PATCH_LIST
	};

	enum class IndexType : uint8_t
	{
		UINT_32, UINT_64
	};

	enum class CullMode : uint8_t
	{
		NONE, FRONT, BACK
	};

	enum class FillMode : uint8_t
	{
		SOLID, WIREFRAME
	};

	enum class FrontFace : uint8_t
	{
		COUNTER_CLOCKWISE, CLOCKWISE
	};

	struct RasterizerState
	{
		FillMode Fill = FillMode::SOLID;
		CullMode Cull = CullMode::BACK;
		FrontFace FrontFace = FrontFace::COUNTER_CLOCKWISE;
		bool DepthClamp = false;
		bool ScissorEnable = true;
		float DepthBiasConstant = 0.0f, DepthBiasSlope = 0.0f, DepthBiasClamp = 0.0f;
		bool operator==(const RasterizerState&) const = default;
	};

	enum class StencilOperator : uint8_t
	{
		KEEP, ZERO, REPLACE, INCR_CLAMP, DECR_CLAMP, INVERT, INCR_WRAP, DECR_WRAP
	};

	struct StencilFaceState
	{
		StencilOperator FailOp = StencilOperator::KEEP, DepthFailOp = StencilOperator::KEEP, PassOp = StencilOperator::KEEP;
		CompareFunc Func = CompareFunc::ALWAYS;
		bool operator==(const StencilFaceState&) const = default;
	};

	struct DepthStencilState 
	{
		bool DepthTest = false, DepthWrite = false;
		CompareFunc DepthFunc = CompareFunc::LESS;
		bool StencilTest = false;
		uint8_t StencilReadMask = 0xFF, StencilWriteMask = 0xFF;
		StencilFaceState Front, Back;
		bool operator==(const DepthStencilState&) const = default;
	};

	enum class BlendFactor : uint8_t
	{
		ZERO, ONE, SRC_COLOR, INV_SRC_COLOR, SRC_ALPHA, INV_SRC_ALPHA, DST_COLOR, INV_DST_COLOR, DST_ALPHA, INV_DST_ALPHA, CONSTANT_COLOR, INV_CONSTANT_COLOR
	};

	enum class BlendOp : uint8_t
	{
		ADD, SUBSTRACT, REVERSE_SUBSTRACT, MIN, MAX
	};

	enum class ColorWriteMask : uint8_t
	{
		NONE = BIT(0), R = BIT(1), G = BIT(2), B = BIT(3), A = BIT(5), ALL = BIT(6)
	};

	struct RenderTargetBlend
	{
		bool BlendEnable = false;
		BlendFactor SrcColor = BlendFactor::ONE, DstColor = BlendFactor::ZERO;
		BlendOp ColorOp = BlendOp::ADD;
		BlendFactor SrcAlpha = BlendFactor::ONE, DstAlpha = BlendFactor::ZERO;
		BlendOp AlphaOp = BlendOp::ADD;
		ColorWriteMask WriteMask = ColorWriteMask::ALL;

		bool operator==(const RenderTargetBlend&) const = default;
	};

	struct BlendState
	{
		bool AlphaToCoverage = false;
		RenderTargetBlend Targets[kMaxColorAttachments];
		bool operator==(const BlendState&) const = default;
	};

	struct RenderTargetFormats
	{
		Format Color[kMaxColorAttachments] = {};
		uint32_t ColorCount = 0;
		Format DepthStencil = Format::UNKNOWN;
		uint32_t SampleCount = 1;
	};

	struct GraphicsPipelineDescription
	{
		Ref<IShader> Vertex, Pixel, Geometry, Hull, Domain;
		VertexInputDescription VertexInput;
		PrimitiveTopology Topology = PrimitiveTopology::TRIANGLE_LIST;
		uint32_t PatchControlPoints = 0;
		RasterizerState Rasterizer;
		DepthStencilState DepthStencil;
		BlendState Blend;
		RenderTargetFormats Targets;
		Ref<IPipelineLayout> Layout;
		std::string DebugName = "Unknown graphics pipeline";
	};

	struct ComputePipelineDescription
	{
		Ref<IShader> Compute;
		Ref<IPipelineLayout> Layout;
		std::string DebugName = "Unknown compute pipeline";
	};

	class IGraphicsPipeline : public IRHIResource
	{
	public:
		virtual const GraphicsPipelineDescription& get_description() const = 0;
	};

	class IComputePipeline : public IRHIResource
	{
	public:
		virtual const ComputePipelineDescription& get_description() const = 0;
	};
}