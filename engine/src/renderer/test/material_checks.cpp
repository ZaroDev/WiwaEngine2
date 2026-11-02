#include <pch.h>
#include "render_test.h"

#include "../render_world.h"
#include "../renderer.h"
#include "../resources/gpu_resource_cache.h"

// Checks of custom materials (materials with a surface shader) on the GPU side: the surface shader cache, the custom records and
// the parameter byte buffer. Each check builds its own queue and cache.
namespace wiwa
{
	namespace
	{
		struct MaterialFixture
		{
			MaterialFixture(RenderCheckContext& context, size_t budget)
			{
				Queue.initialize(context.Device, context.HasCopyQueue, budget);
				Cache = std::make_unique<GpuResourceCache>(context.Device, context.World.get_renderer().get_assets(), &Queue, context.Api);
			}

			~MaterialFixture()
			{
				Cache.reset();
				Queue.shutdown();
			}

			void frame()
			{
				Queue.begin_frame();
				Cache->update();
				Queue.submit();
			}

			template<typename Predicate>
			bool pump_until(Predicate done, u32 maxFrames = 120)
			{
				for (u32 i = 0; i < maxFrames && !done(); i++)
				{
					frame();
					std::this_thread::sleep_for(std::chrono::milliseconds(1)); // asset loads finish on worker threads
				}
				return done();
			}

			UploadQueue Queue{};
			std::unique_ptr<GpuResourceCache> Cache{};
		};

		// A surface shader with real binaries: the engine's own entry templates (engine/shaders/surface_*.hlsl), which the engine
		// cooks with the default surface. Every variant, both stages, both targets.
		bool make_test_shader(RenderCheckContext& context, SurfaceShaderData& shader)
		{
			AssetManager* assets = context.World.get_renderer().get_assets();
			if (assets == nullptr)
			{
				return false;
			}

			const char* const templates[SURFACE_VARIANT_COUNT] = {
				"engine/shaders/surface_forward.hlsl", "engine/shaders/surface_depth.hlsl", "engine/shaders/surface_depth.hlsl",
				"engine/shaders/surface_velocity.hlsl", "engine/shaders/surface_shadow.hlsl", "engine/shaders/surface_shadow.hlsl",
			};

			shader = SurfaceShaderData{};
			shader.Name = "test surface shader";
			for (u32 kind = 0; kind < SURFACE_VARIANT_COUNT; kind++)
			{
				const std::optional<AssetRef> ref = assets->find_asset(templates[kind]);
				if (!ref)
				{
					return false;
				}
				const Handle<ShaderData> handle = assets->load<ShaderData>(*ref);
				assets->wait_idle();
				const ShaderData* data = handle.get();
				if (data == nullptr)
				{
					return false;
				}
				for (const ShaderBinary& binary : data->Binaries)
				{
					if (binary.Stage == ShaderStage::Vertex || binary.Stage == ShaderStage::Pixel)
					{
						shader.Variants.push_back(SurfaceVariant{ static_cast<SurfaceVariantKind>(kind), binary.Stage, binary.Target, binary.Code });
					}
				}
			}
			return true;
		}

		std::vector<u8> pattern_bytes(u32 count, u32 salt)
		{
			std::vector<u8> bytes(count);
			for (u32 i = 0; i < count; i++)
			{
				bytes[i] = static_cast<u8>((i * 7 + 3 + salt * 29) & 0xff);
			}
			return bytes;
		}

		MaterialData custom_material(const AssetRef& shader, std::vector<u8> params)
		{
			MaterialData material;
			material.BaseColorFactor = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
			material.Shader = shader;
			material.ParamBytes = std::move(params);
			return material;
		}

		bool is_magenta(const GpuMaterialRecord& record)
		{
			return record.BaseColor == glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
		}
	}

	void register_material_checks()
	{
		register_render_check("custom_material_record_and_bytes", [](RenderCheckContext& context)
			{
				MaterialFixture fixture(context, 4 * 1024 * 1024);
				GpuResourceCache& cache = *fixture.Cache;
				context.check(cache.custom_layout() != nullptr, "the custom layout exists");

				SurfaceShaderData shader;
				context.check(make_test_shader(context, shader), "the entry templates make a test shader");
				shader.BlockSize = 32;
				shader.Textures = { "Mask" };
				const AssetRef shaderRef = cache.surface_shaders().add_inline_shader(shader);

				// Enough materials that the parameter buffer has to grow.
				constexpr u32 COUNT = 300;
				std::vector<u32> slots;
				for (u32 i = 0; i < COUNT; i++)
				{
					slots.push_back(cache.get_material_slot(cache.add_inline_material(custom_material(shaderRef, pattern_bytes(32, i)))));
				}
				const u32 builtin = cache.get_material_slot(cache.add_inline_material(MaterialData{}));
				const u32 generation = cache.material_generation();

				context.check(fixture.pump_until([&]
					{
						for (u32 slot : slots)
						{
							if (cache.get_material_shader(slot) == 0)
							{
								return false;
							}
						}
						return true;
					}), "every custom material reports its shader");
				fixture.frame(); // the last writes are on the GPU
				context.check(cache.material_generation() != generation, "growing the buffers changes the material generation");

				const u32 shaderIndex = cache.get_material_shader(slots[0]);
				const GpuSurfaceShader* gpu = cache.surface_shaders().get(shaderIndex);
				context.check(gpu != nullptr && gpu->ready() && gpu->TextureCount == 1, "the shader is ready with one texture");
				context.check(gpu != nullptr && gpu->get(SurfaceVariantKind::Forward, ShaderStage::Pixel) != nullptr, "it has a forward pixel shader");
				context.check(cache.get_material_shader(builtin) == 0, "a built-in material has no shader");
				context.check((cache.material_record(builtin).Flags & MATERIAL_CUSTOM_BIT) == 0, "and no custom bit");
				context.check(cache.custom_record(builtin).ParamSize == 0, "and an empty custom record");
				context.check(cache.get_custom_bindings(builtin) != nullptr && cache.get_custom_bindings(slots[0]) != nullptr, "custom binding sets exist");

				std::vector<u8> records;
				std::vector<u8> params;
				const u32 maxSlot = *std::max_element(slots.begin(), slots.end());
				context.check(read_buffer_bytes(context.Device, cache.custom_record_buffer(), static_cast<u64>(maxSlot + 1) * sizeof(GpuCustomRecord), records), "read the custom records back");
				context.check(read_buffer_bytes(context.Device, cache.param_buffer(), cache.param_buffer_size(), params), "read the parameter bytes back");
				if (records.size() != static_cast<size_t>(maxSlot + 1) * sizeof(GpuCustomRecord) || params.size() != cache.param_buffer_size())
				{
					return;
				}

				u32 wrongFlags = 0;
				u32 wrongRecords = 0;
				u32 wrongBytes = 0;
				std::vector<std::pair<u32, u32>> ranges;
				for (u32 i = 0; i < COUNT; i++)
				{
					const u32 slot = slots[i];
					wrongFlags += (cache.material_record(slot).Flags & MATERIAL_CUSTOM_BIT) != 0 ? 0 : 1;

					GpuCustomRecord onGpu;
					std::memcpy(&onGpu, records.data() + static_cast<size_t>(slot) * sizeof(GpuCustomRecord), sizeof(onGpu));
					const GpuCustomRecord& onCpu = cache.custom_record(slot);
					if (onGpu.ParamOffset != onCpu.ParamOffset || onGpu.ParamSize != 32 || onCpu.ParamSize != 32 || onGpu.ParamOffset % 16 != 0)
					{
						wrongRecords++;
						continue;
					}
					const std::vector<u8> expected = pattern_bytes(32, i);
					if (onGpu.ParamOffset + 32 > params.size() || std::memcmp(params.data() + onGpu.ParamOffset, expected.data(), 32) != 0)
					{
						wrongBytes++;
					}
					ranges.emplace_back(onGpu.ParamOffset, onGpu.ParamOffset + 32);
				}
				context.check(wrongFlags == 0, std::to_string(wrongFlags) + " custom materials without the custom bit");
				context.check(wrongRecords == 0, std::to_string(wrongRecords) + " custom records wrong on the GPU");
				context.check(wrongBytes == 0, std::to_string(wrongBytes) + " parameter blocks not at their record's offset");

				std::sort(ranges.begin(), ranges.end());
				bool overlap = false;
				for (size_t i = 1; i < ranges.size(); i++)
				{
					overlap = overlap || ranges[i].first < ranges[i - 1].second;
				}
				context.check(!overlap, "parameter blocks do not overlap");

				// A parameter edit is written where the record points.
				const AssetRef edited = cache.add_inline_material(custom_material(shaderRef, pattern_bytes(32, 1000)));
				const u32 editedSlot = cache.get_material_slot(edited);
				context.check(fixture.pump_until([&] { return cache.get_material_shader(editedSlot) != 0; }), "one more custom material");
				cache.set_inline_material(edited, custom_material(shaderRef, pattern_bytes(32, 2000)));
				cache.invalidate(edited.resolved());
				fixture.frame();
				fixture.frame();
				const u32 offset = cache.custom_record(editedSlot).ParamOffset;
				context.check(read_buffer_bytes(context.Device, cache.param_buffer(), cache.param_buffer_size(), params), "read the bytes again");
				const std::vector<u8> expected = pattern_bytes(32, 2000);
				context.check(offset + 32 <= params.size() && std::memcmp(params.data() + offset, expected.data(), 32) == 0, "the edited block holds the new bytes");
			});

		register_render_check("custom_material_failed_is_magenta_slot", [](RenderCheckContext& context)
			{
				MaterialFixture fixture(context, 1024 * 1024);
				GpuResourceCache& cache = *fixture.Cache;

				// A shader with no binaries (an incomplete pak, say) and one that is not an asset at all.
				SurfaceShaderData empty;
				empty.Name = "no variants";
				empty.BlockSize = 16;
				const AssetRef incomplete = cache.surface_shaders().add_inline_shader(empty);
				AssetRef missing;
				missing.Guid = AssetGuid::generate();

				const u32 a = cache.get_material_slot(cache.add_inline_material(custom_material(incomplete, pattern_bytes(16, 1))));
				const u32 b = cache.get_material_slot(cache.add_inline_material(custom_material(missing, pattern_bytes(16, 2))));
				const u32 indexA = cache.surface_shaders().get_shader_index(incomplete);
				const u32 indexB = cache.surface_shaders().get_shader_index(missing);
				context.check(indexA != 0 && indexB != 0 && indexA != indexB, "each shader has its own index");

				context.check(fixture.pump_until([&]
					{
						const GpuSurfaceShader* sa = cache.surface_shaders().get(indexA);
						const GpuSurfaceShader* sb = cache.surface_shaders().get(indexB);
						return sa != nullptr && sa->Failed && sb != nullptr && sb->Failed;
					}, 3000), "both shaders fail");
				fixture.frame();

				for (const u32 slot : { a, b })
				{
					const std::string name = "slot " + std::to_string(slot);
					context.check(is_magenta(cache.material_record(slot)), name + " shows magenta");
					context.check((cache.material_record(slot).Flags & MATERIAL_CUSTOM_BIT) == 0, name + " has no custom bit");
					context.check(cache.get_material_shader(slot) == 0, name + " reports no shader");
					context.check(cache.custom_record(slot).ParamSize == 0, name + " has an empty custom record");
					context.check(cache.get_custom_bindings(slot) != nullptr, name + " still has a binding set");
				}
			});

		register_render_check("custom_material_generation_changes_on_reload", [](RenderCheckContext& context)
			{
				MaterialFixture fixture(context, 1024 * 1024);
				GpuResourceCache& cache = *fixture.Cache;
				SurfaceShaderCache& shaders = cache.surface_shaders();

				SurfaceShaderData shader;
				context.check(make_test_shader(context, shader), "the entry templates make a test shader");
				shader.BlockSize = 16;
				const AssetRef ref = shaders.add_inline_shader(shader);
				const u32 slot = cache.get_material_slot(cache.add_inline_material(custom_material(ref, pattern_bytes(16, 5))));

				const u32 before = shaders.generation();
				context.check(fixture.pump_until([&] { return cache.get_material_shader(slot) != 0; }), "the material draws custom");
				context.check(shaders.generation() != before, "a shader appearing changes the generation");

				const u32 index = cache.get_material_shader(slot);
				const GpuSurfaceShader* gpu = shaders.get(index);
				const u32 shaderGeneration = gpu != nullptr ? gpu->Generation : 0;
				const nvrhi::IShader* oldForward = gpu != nullptr ? gpu->get(SurfaceVariantKind::Forward, ShaderStage::Pixel) : nullptr;

				// A reload: the generation moves, the index stays.
				const u32 beforeReload = shaders.generation();
				shaders.set_inline_shader(ref, shader);
				cache.invalidate(ref.resolved());
				fixture.frame();
				gpu = shaders.get(index);
				context.check(shaders.generation() != beforeReload, "a reload changes the generation");
				context.check(shaders.get_shader_index(ref) == index && cache.get_material_shader(slot) == index, "the index stays");
				context.check(gpu != nullptr && gpu->Generation > shaderGeneration, "the shader's own generation goes up");
				context.check(gpu != nullptr && gpu->get(SurfaceVariantKind::Forward, ShaderStage::Pixel) != oldForward, "with new shader objects");

				// A broken reload keeps the version in use.
				const u32 beforeBroken = shaders.generation();
				const u32 workingGeneration = gpu != nullptr ? gpu->Generation : 0;
				SurfaceShaderData broken;
				broken.Name = "broken";
				shaders.set_inline_shader(ref, broken);
				cache.invalidate(ref.resolved());
				fixture.frame();
				gpu = shaders.get(index);
				context.check(gpu != nullptr && gpu->ready() && gpu->Generation == workingGeneration, "a broken reload keeps the old version");
				context.check(shaders.generation() == beforeBroken, "and changes nothing");
				context.check(cache.get_material_shader(slot) == index && (cache.material_record(slot).Flags & MATERIAL_CUSTOM_BIT) != 0, "the material still draws custom");
			});
	}
}
