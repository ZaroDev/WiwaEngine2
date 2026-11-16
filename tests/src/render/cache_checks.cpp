#include <pch.h>
#include "render_test.h"

#include <renderer/render_world.h>
#include <renderer/renderer.h>
#include <renderer/resources/gpu_resource_cache.h>
#include "test_meshes.h"

// Checks of the GPU resource cache and the uploads behind it. Each check builds its own queue and cache.
namespace wiwa
{
	namespace
	{
		struct CacheFixture
		{
			CacheFixture(RenderCheckContext& context, size_t budget)
			{
				Queue.initialize(context.Device, context.HasCopyQueue, budget);
				Cache = std::make_unique<GpuResourceCache>(context.Device, context.World.get_renderer().get_assets(), &Queue, context.Api);
			}

			~CacheFixture()
			{
				Cache.reset();
				Queue.shutdown();
			}

			// One frame of the cache's life: what Renderer::render_frame does around update().
			void frame()
			{
				Queue.begin_frame();
				Cache->update();
				Queue.submit();
			}

			void pump(u32 frames)
			{
				for (u32 i = 0; i < frames; i++)
				{
					frame();
				}
			}

			template<typename Predicate>
			bool pump_until(Predicate done, u32 maxFrames = 60)
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

		TextureData make_solid_texture(u8 r, u8 g, u8 b, u8 a, TextureKind kind = TextureKind::Color)
		{
			TextureData texture;
			texture.Width = 2;
			texture.Height = 2;
			texture.Kind = kind;
			texture.Mips.push_back(std::vector<u8>{ r, g, b, a, r, g, b, a, r, g, b, a, r, g, b, a });
			texture.Mips.push_back(std::vector<u8>{ r, g, b, a });
			return texture;
		}

		bool is_near(float a, float b)
		{
			return std::abs(a - b) < 1e-5f;
		}
	}

	void register_cache_checks()
	{
		register_render_check("cache_mesh_upload", [](RenderCheckContext& context)
			{
				CacheFixture fixture(context, 1024 * 1024);
				const AssetRef mesh = fixture.Cache->add_inline_mesh(make_test_cube({}));
				context.check(fixture.Cache->get_mesh(mesh) == nullptr, "not uploaded before the first update");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_mesh(mesh) != nullptr; }), "uploaded within a few frames");

				const GpuMesh* gpu = fixture.Cache->get_mesh(mesh);
				if (gpu == nullptr)
				{
					return;
				}
				context.check(gpu->Primitives.size() == 1, "one primitive");
				context.check(gpu->Primitives[0].IndexCount == 36, "36 indices");
				context.check(gpu->BoundsMin == glm::vec3(-1.0f) && gpu->BoundsMax == glm::vec3(1.0f), "bounds -1..1");
				context.check(gpu->Primitives[0].MaterialSlot == 0, "no material is the default slot");
				context.check(fixture.Cache->get_stats().Meshes == 1, "stats count one mesh");

				// What is in the GPU buffers is the mesh.
				const MeshData cube = make_test_cube({});
				std::vector<u8> bytes;
				context.check(read_buffer_bytes(context.Device, fixture.Cache->index_buffer(gpu->Page), 36 * sizeof(u32), bytes), "read the indices back");
				context.check(bytes.size() == 36 * sizeof(u32) && std::memcmp(bytes.data(), cube.Indices.data(), bytes.size()) == 0, "indices equal the mesh's");
				context.check(read_buffer_bytes(context.Device, fixture.Cache->vertex_buffer(gpu->Page), 24 * sizeof(MeshVertex), bytes), "read the vertices back");
				context.check(bytes.size() == 24 * sizeof(MeshVertex) && std::memcmp(bytes.data(), cube.Vertices.data(), bytes.size()) == 0, "vertices equal the mesh's");
			});

		register_render_check("cache_material_fallback", [](RenderCheckContext& context)
			{
				CacheFixture fixture(context, 1024 * 1024);
				const AssetRef texture = fixture.Cache->add_inline_texture(make_solid_texture(255, 0, 0, 255));

				MaterialData material;
				material.BaseColorFactor = glm::vec4(0.5f, 0.25f, 1.0f, 1.0f);
				material.Alpha = AlphaMode::Mask;
				material.AlphaCutoff = 0.4f;
				material.DoubleSided = true;
				material.BaseColorTexture = texture;
				const AssetRef materialRef = fixture.Cache->add_inline_material(material);

				const u32 slot = fixture.Cache->get_material_slot(materialRef);
				context.check(slot != 0, "a real slot");
				context.check(fixture.Cache->get_material_slot(materialRef) == slot, "asking again gives the same slot");
				context.check(fixture.Cache->get_material_slot(AssetRef{}) == 0, "no material is slot 0");
				nvrhi::IBindingSet* fallback = fixture.Cache->get_material_bindings(0);
				context.check(fallback != nullptr && fixture.Cache->get_material_bindings(slot) == fallback, "the fallback set stands in at first");

				fixture.frame();
				nvrhi::IBindingSet* firstSet = fixture.Cache->get_material_bindings(slot);
				context.check(firstSet != nullptr, "a binding set of its own while the texture loads");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Textures == 1; }), "the texture is uploaded");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_material_bindings(slot) != firstSet; }), "the binding set switches to the real texture");

				nvrhi::ITexture* gpuTexture = fixture.Cache->get_texture(texture);
				context.check(gpuTexture != nullptr, "the texture can be found");
				if (gpuTexture != nullptr)
				{
					std::vector<u8> pixels;
					u32 width = 0;
					u32 height = 0;
					context.check(read_texture_rgba8(context.Device, gpuTexture, pixels, width, height) && width == 2 && pixels.size() == 16 && pixels[0] == 255 && pixels[1] == 0
						&& pixels[2] == 0, "its pixels are the red ones");
				}

				fixture.pump(2); // the record is written once the texture is known
				std::vector<u8> bytes;
				context.check(read_buffer_bytes(context.Device, fixture.Cache->material_buffer(), 64ull * (slot + 1), bytes), "read the records back");
				if (bytes.size() == 64ull * (slot + 1))
				{
					GpuMaterialRecord record;
					std::memcpy(&record, bytes.data() + 64ull * slot, sizeof(record));
					context.check(is_near(record.BaseColor.x, 0.5f) && is_near(record.BaseColor.y, 0.25f), "the base color factor");
					context.check(is_near(record.AlphaCutoff, 0.4f), "the cutoff");
					context.check((record.Flags & MATERIAL_ALPHA_MASK_BIT) != 0 && (record.Flags & MATERIAL_DOUBLE_SIDED_BIT) != 0, "mask and double sided flags");
					context.check((record.TextureMask & MATERIAL_HAS_BASE_COLOR) != 0 && (record.TextureMask & MATERIAL_HAS_NORMAL) == 0, "only the base color is a real texture");
				}
			});

		register_render_check("cache_failed_asset", [](RenderCheckContext& context)
			{
				CacheFixture fixture(context, 1024 * 1024);
				AssetRef missing;
				missing.Guid = AssetGuid::generate();
				context.check(fixture.Cache->get_mesh(missing) == nullptr, "null at first");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Failed == 1; }, 3000), "the failure is noticed");
				context.check(fixture.Cache->get_mesh(missing) == nullptr, "and stays null");
				fixture.pump(10);
				context.check(fixture.Cache->get_stats().Failed == 1 && fixture.Cache->get_mesh(missing) == nullptr, "counted once");

				AssetRef missingMaterial;
				missingMaterial.Guid = AssetGuid::generate();
				const u32 slot = fixture.Cache->get_material_slot(missingMaterial);
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Failed == 2; }, 3000), "a missing material fails too");
				std::vector<u8> bytes;
				if (read_buffer_bytes(context.Device, fixture.Cache->material_buffer(), 64ull * (slot + 1), bytes) && bytes.size() == 64ull * (slot + 1))
				{
					GpuMaterialRecord record;
					std::memcpy(&record, bytes.data() + 64ull * slot, sizeof(record));
					context.check(record.BaseColor == glm::vec4(1.0f, 0.0f, 1.0f, 1.0f), "it shows magenta");
				}
			});

		register_render_check("cache_invalidate", [](RenderCheckContext& context)
			{
				CacheFixture fixture(context, 1024 * 1024);
				const AssetRef mesh = fixture.Cache->add_inline_mesh(make_test_cube({}));
				fixture.Cache->get_mesh(mesh);
				context.check(fixture.pump_until([&] { return fixture.Cache->get_mesh(mesh) != nullptr; }), "uploaded");
				context.check(fixture.Cache->get_stats().Reloads == 0, "no reloads yet");

				fixture.Cache->invalidate(mesh.resolved());
				context.check(fixture.Cache->get_mesh(mesh) != nullptr, "the old version is used while the new one loads");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Reloads == 1; }), "the reload is counted");
				context.check(fixture.Cache->get_mesh(mesh) != nullptr && fixture.Cache->get_stats().Meshes == 1, "still one mesh, valid");

				fixture.pump(12); // the old ranges are given back after the frames in flight are done
				context.check(fixture.Cache->get_stats().Bytes == 24 * sizeof(MeshVertex) + 36 * sizeof(u32), "only the new copy is kept");
			});

		register_render_check("cache_budget", [](RenderCheckContext& context)
			{
				CacheFixture fixture(context, 1000);
				std::vector<AssetRef> meshes;
				for (int i = 0; i < 10; i++)
				{
					meshes.push_back(fixture.Cache->add_inline_mesh(make_test_cube({})));
					fixture.Cache->get_mesh(meshes.back());
				}

				fixture.frame();
				context.check(fixture.Cache->get_stats().Meshes < 10, "not everything fits in the first frame");
				u32 frames = 1;
				while (fixture.Cache->get_stats().Meshes < 10 && frames < 60)
				{
					fixture.frame();
					frames++;
				}
				context.check(fixture.Cache->get_stats().Meshes == 10, "all of them arrive");
				context.check(frames >= 5, "spread over several frames");
			});

		register_render_check("cache_suballocation", [](RenderCheckContext& context)
			{
				RangeAllocator ranges(100);
				const std::optional<u64> a = ranges.allocate(30);
				const std::optional<u64> b = ranges.allocate(30);
				const std::optional<u64> c = ranges.allocate(30);
				context.check(a && b && c && *a == 0 && *b == 30 && *c == 60, "first fit from the start");
				context.check(!ranges.allocate(20), "no room for 20 more");
				ranges.free(*a, 30);
				ranges.free(*c, 30);
				ranges.free(*b, 30);
				context.check(ranges.used() == 0, "all free");
				context.check(ranges.allocate(100) == std::optional<u64>(0), "merged back into one range");

				CacheFixture fixture(context, 1024 * 1024);
				std::vector<AssetRef> meshes;
				for (int i = 0; i < 4; i++)
				{
					meshes.push_back(fixture.Cache->add_inline_mesh(make_test_cube({})));
					fixture.Cache->get_mesh(meshes.back());
				}
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Meshes == 4; }), "four meshes uploaded");

				fixture.Cache->invalidate(meshes[1].resolved()); // moved to a new place, the old range freed later
				fixture.pump(12);
				bool disjoint = true;
				for (size_t i = 0; i < meshes.size(); i++)
				{
					for (size_t j = i + 1; j < meshes.size(); j++)
					{
						const GpuMesh* first = fixture.Cache->get_mesh(meshes[i]);
						const GpuMesh* second = fixture.Cache->get_mesh(meshes[j]);
						if (first == nullptr || second == nullptr)
						{
							disjoint = false;
							continue;
						}
						const GpuPrimitive& p = first->Primitives[0];
						const GpuPrimitive& q = second->Primitives[0];
						const bool overlapIndices = first->Page == second->Page && p.FirstIndex < q.FirstIndex + q.IndexCount && q.FirstIndex < p.FirstIndex + p.IndexCount;
						const bool overlapVertices = first->Page == second->Page && p.BaseVertex < q.BaseVertex + 24 && q.BaseVertex < p.BaseVertex + 24;
						disjoint = disjoint && !overlapIndices && !overlapVertices;
					}
				}
				context.check(disjoint, "no two meshes share a range");
			});
	}
}
