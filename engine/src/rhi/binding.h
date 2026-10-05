#pragma once
#include "rhi_common.h"
#include "rhi_resource.h"
#include "shader.h"

#include <vector>

namespace wiwa
{
	struct BindingLayoutEntry
	{
		uint32_t Binding = 0;
		BindingType Type = BindingType::UNIFORM_BUFFER;
		uint32_t Count = 1;
		ShaderStage Visibility = ShaderStage::ALL;
		bool DynamicOffset = false; // Buffers only, offset supplied at set_binding_set() time
	};

	struct BindingLayoutDescription
	{
		std::vector<BindingLayoutEntry> Entries;
		std::string DebugName = "Unkown binding layout";
	};

	class IBindingLayout : public IRHIResource
	{
	public:
		virtual const BindingLayoutDescription& get_description() const = 0;
	};

	struct PushConstantRange
	{
		ShaderStage Stages = ShaderStage::ALL;
		uint32_t Offset = 0;
		uint64_t Size = 0;
	};

	struct PipelineLayoutDescription
	{
		std::vector<Ref<IBindingLayout>> BindingLayouts;
		std::vector<PushConstantRange> PushConstants;
		std::string DebugName = "Unknown pipeline layout";
	};

	class IPipelineLayout : public IRHIResource
	{
	public:
		virtual const PipelineLayoutDescription& get_description() const = 0;
	};

	struct BindingEntry
	{
		uint32_t Binding = 0;
		uint32_t ArrayElement = 0;
		BindingType Type = BindingType::UNIFORM_BUFFER;
		Ref<IBuffer> Buffer = nullptr;
		uint64_t BufferOffset = 0;
		uint64_t BufferRange = kRemainingSize;
		Ref<ITextureView> TextureView;
		Ref<ISampler> Sampler;
	};

	struct BindingSetDescription
	{
		Ref<IBindingLayout> Layout;
		std::vector<BindingEntry> Entries;
		std::string DebugName = "Unknown binding set";
	};

	class IBindingSet : public IRHIResource
	{
	public:
		virtual const BindingSetDescription& get_description() const = 0;
	};
}