#include <pch.h>
#include "render_test.h"

#include <renderer/render_world.h>
#include <renderer/renderer.h>
#include <renderer/resources/gpu_resource_cache.h>
#include "test_meshes.h"

#include <asset_pipeline/image_utils.h>
#include <asset_editor/mesh_edit_model.h>
#include <asset_pipeline/mesh_lods.h>
#include <assets/wasset_io.h>
#include <renderer/scene/lod_select.h>
#include <renderer/scene/render_scene.h>
#include <bc7decomp.h>

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

		// A UV sphere of radius 1: smooth, so the simplifier has something to remove.
		MeshData make_test_sphere(u32 rings, u32 segments)
		{
			MeshData mesh;
			mesh.Name = "test sphere";
			for (u32 y = 0; y <= rings; y++)
			{
				const float v = static_cast<float>(y) / static_cast<float>(rings);
				const float phi = v * 3.14159265f;
				for (u32 x = 0; x <= segments; x++)
				{
					const float u = static_cast<float>(x) / static_cast<float>(segments);
					const float theta = u * 6.2831853f;
					MeshVertex vertex;
					vertex.Normal[0] = std::sin(phi) * std::cos(theta);
					vertex.Normal[1] = std::cos(phi);
					vertex.Normal[2] = std::sin(phi) * std::sin(theta);
					for (int k = 0; k < 3; k++)
					{
						vertex.Position[k] = vertex.Normal[k];
					}
					vertex.Uv[0] = u;
					vertex.Uv[1] = v;
					mesh.Vertices.push_back(vertex);
				}
			}
			for (u32 y = 0; y < rings; y++)
			{
				for (u32 x = 0; x < segments; x++)
				{
					const u32 a = y * (segments + 1) + x;
					const u32 b = a + segments + 1;
					for (u32 index : { a, b, a + 1, a + 1, b, b + 1 })
					{
						mesh.Indices.push_back(index);
					}
				}
			}
			MeshPrimitive primitive;
			primitive.IndexCount = static_cast<u32>(mesh.Indices.size());
			primitive.VertexCount = static_cast<u32>(mesh.Vertices.size());
			primitive.BoundsMin = glm::vec3(-1.0f);
			primitive.BoundsMax = glm::vec3(1.0f);
			primitive.Lods[0] = MeshLod{ 0, primitive.IndexCount, 0.0f };
			mesh.Primitives.push_back(primitive);
			mesh.BoundsMin = glm::vec3(-1.0f);
			mesh.BoundsMax = glm::vec3(1.0f);
			return mesh;
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

		register_render_check("cache_bc7_texture", [](RenderCheckContext& context)
			{
				// 8x8 diagonal gradient with a full chain: 8, 4, 2, 1
				TextureData texture;
				texture.Width = 8;
				texture.Height = 8;
				texture.Kind = TextureKind::Color;
				for (u32 mip = 0; mip < 4; mip++)
				{
					const u32 size = 8u >> mip;
					std::vector<u8> level(static_cast<size_t>(size) * size * 4);
					for (u32 y = 0; y < size; y++)
					{
						for (u32 x = 0; x < size; x++)
						{
							u8* texel = level.data() + (static_cast<size_t>(y) * size + x) * 4;
							const u32 ramp = (x + y) * 255 / std::max(2 * (size - 1), 1u); // one color line: what a BC7 block holds best
							texel[0] = static_cast<u8>(ramp);
							texel[1] = static_cast<u8>(255 - ramp);
							texel[2] = 128;
							texel[3] = 255;
						}
					}
					texture.Mips.push_back(std::move(level));
				}
				const std::vector<u8> original = texture.Mips[0];

				context.check(compress_texture_bc7(texture), "an 8x8 texture compresses");
				context.check(texture.Codec == TextureCodec::Bc7 && texture.Mips.size() == 4, "it is BC7 with its chain");
				context.check(texture.Mips[0].size() == 64 && texture.Mips[1].size() == 16 && texture.Mips[3].size() == 16, "a block per 4x4 texels, one block for the small levels");
				context.check(!compress_texture_bc7(texture), "a BC7 texture is not compressed again");

				// the first level decodes back to something close to the source
				int worst = 0;
				for (u32 block = 0; block < 4; block++)
				{
					bc7decomp::color_rgba texels[16];
					context.check(bc7decomp::unpack_bc7(texture.Mips[0].data() + block * 16, texels), "the block is a valid BC7 block");
					for (u32 i = 0; i < 16; i++)
					{
						const u32 x = (block % 2) * 4 + i % 4;
						const u32 y = (block / 2) * 4 + i / 4;
						const u8* expected = original.data() + (static_cast<size_t>(y) * 8 + x) * 4;
						for (u32 channel = 0; channel < 4; channel++)
						{
							worst = std::max<int>(worst, std::abs(static_cast<int>(texels[i].m_comps[channel]) - static_cast<int>(expected[channel])));
						}
					}
				}
				context.check(worst <= 6, "the decoded texels are within 6 of the source (worst " + std::to_string(worst) + ")");

				TextureData odd;
				odd.Width = 6;
				odd.Height = 6;
				odd.Mips.push_back(std::vector<u8>(6 * 6 * 4, 200));
				context.check(!compress_texture_bc7(odd) && odd.Codec == TextureCodec::Rgba8, "a size that is not a multiple of 4 stays RGBA8");

				CacheFixture fixture(context, 1024 * 1024);
				const AssetRef ref = fixture.Cache->add_inline_texture(texture);
				context.check(fixture.pump_until([&] { return fixture.Cache->get_texture(ref) != nullptr; }), "the BC7 texture is uploaded");
				nvrhi::ITexture* gpu = fixture.Cache->get_texture(ref);
				if (gpu != nullptr)
				{
					context.check(gpu->getDesc().format == nvrhi::Format::BC7_UNORM_SRGB && gpu->getDesc().mipLevels == 4, "as sRGB BC7 with 4 levels");
				}
				context.check(fixture.Cache->get_stats().Bytes == 64 + 16 * 3, "its memory is the blocks, a quarter of RGBA8");

				// a block codec with a size that is not a multiple of 4 is refused, not uploaded
				TextureData broken = texture;
				broken.Width = 6;
				broken.Height = 6;
				const AssetRef brokenRef = fixture.Cache->add_inline_texture(broken);
				context.check(fixture.Cache->get_texture(brokenRef) == nullptr, "nothing at first"); // asking is what starts the upload
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Failed == 1; }) && fixture.Cache->get_texture(brokenRef) == nullptr, "a malformed BC7 texture fails");
			});

		register_render_check("cache_mesh_lods", [](RenderCheckContext& context)
			{
				MeshData mesh = make_test_sphere(32, 64);
				const std::vector<u32> fullIndices = mesh.Indices;
				const u32 fullCount = mesh.Primitives[0].IndexCount;

				const LodBakeResult result = bake_mesh_lods(mesh, LodBakeSettings{});
				const MeshPrimitive& primitive = mesh.Primitives[0];
				context.check(result.Primitives == 1 && result.Levels == 3 && primitive.LodCount == 4, "a smooth sphere gets the three levels");
				context.check(primitive.Lods[0].FirstIndex == 0 && primitive.Lods[0].IndexCount == fullCount && primitive.IndexCount == fullCount
					&& std::equal(fullIndices.begin(), fullIndices.end(), mesh.Indices.begin()), "level 0 is the mesh as it was");
				bool decreasing = true;
				bool errorsGrow = true;
				bool inRange = true;
				for (u32 level = 1; level < primitive.LodCount; level++)
				{
					decreasing = decreasing && primitive.Lods[level].IndexCount < primitive.Lods[level - 1].IndexCount && primitive.Lods[level].IndexCount % 3 == 0;
					errorsGrow = errorsGrow && primitive.Lods[level].Error > primitive.Lods[level - 1].Error;
					const u64 end = static_cast<u64>(primitive.Lods[level].FirstIndex) + primitive.Lods[level].IndexCount;
					inRange = inRange && end <= mesh.Indices.size();
					for (u64 i = primitive.Lods[level].FirstIndex; inRange && i < end; i++)
					{
						inRange = mesh.Indices[i] < primitive.VertexCount;
					}
				}
				context.check(decreasing, "every level has fewer triangles than the one before (" + std::to_string(fullCount / 3) + " > " + std::to_string(primitive.Lods[1].IndexCount / 3)
					+ " > " + std::to_string(primitive.Lods[2].IndexCount / 3) + " > " + std::to_string(primitive.Lods[3].IndexCount / 3) + ")");
				context.check(errorsGrow && primitive.Lods[3].Error < 0.5f, "the errors grow with the level and stay small");
				context.check(inRange, "the level indices stay inside the primitive's vertices");

				// the file keeps them
				BinaryArchive writer = BinaryArchive::writer();
				archive(writer, mesh);
				const std::vector<std::byte> bytes = writer.take();
				BinaryArchive reader = BinaryArchive::reader(bytes);
				MeshData loaded;
				archive(reader, loaded);
				context.check(reader.ok() && loaded.Primitives.size() == 1 && loaded.Primitives[0].LodCount == 4 && loaded.Indices == mesh.Indices
					&& loaded.Primitives[0].Lods[3].IndexCount == primitive.Lods[3].IndexCount, "the levels survive the asset file");

				// on the GPU, and picked by distance
				CacheFixture fixture(context, 8 * 1024 * 1024);
				const AssetRef ref = fixture.Cache->add_inline_mesh(mesh);
				context.check(fixture.pump_until([&] { return fixture.Cache->get_mesh(ref) != nullptr; }), "the mesh with levels is uploaded");
				const GpuMesh* gpu = fixture.Cache->get_mesh(ref);
				if (gpu != nullptr && gpu->Primitives.size() == 1)
				{
					const GpuPrimitive& drawn = gpu->Primitives[0];
					context.check(drawn.LodCount == 4 && drawn.Lods[0].IndexCount == fullCount && drawn.Lods[3].IndexCount == primitive.Lods[3].IndexCount
						&& drawn.Lods[3].FirstIndex == drawn.Lods[0].FirstIndex + primitive.Lods[3].FirstIndex, "the GPU primitive has its levels, placed with the mesh");

					LodView view;
					view.PixelsPerUnit = 0.5f * 1080.0f / std::tan(glm::radians(30.0f)); // 60 degrees, 1080 pixels
					view.PixelError = 1.5f;
					const auto at = [&](float distance) { return select_mesh_lod(drawn, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -distance)), view); };
					context.check(at(2.5f) == 0, "close up the full mesh is drawn");
					context.check(at(2000.0f) == 3, "far away the coarsest level is drawn");
					context.check(at(40.0f) >= at(10.0f) && at(200.0f) >= at(40.0f), "the level never gets finer with distance");
					context.check(select_mesh_lod(drawn, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2000.0f)), LodView{ glm::vec3(0.0f), view.PixelsPerUnit, false, 0.0f }) == 0,
						"a pixel error of 0 turns the levels off");
					context.check(select_mesh_lod(drawn, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2000.0f)), glm::vec3(1000.0f)), view) < 3,
						"a big instance needs finer levels than a small one at the same distance");
				}

				clear_mesh_lods(mesh);
				context.check(mesh.Indices == fullIndices && mesh.Primitives[0].LodCount == 1, "clearing the levels gives the mesh back");

				// the Mesh editor's document: baking is an undoable edit
				MeshEditModel model(make_test_sphere(32, 64));
				context.check(!model.dirty() && !model.clear_lods(), "a mesh without levels has nothing to clear");
				const LodBakeResult baked = model.bake_lods(LodBakeSettings{});
				context.check(baked.Levels == 3 && model.dirty() && model.data().Primitives[0].LodCount == 4 && model.data().Indices.size() > fullIndices.size(), "baking in the editor adds the levels");
				model.undo();
				context.check(model.data().Primitives[0].LodCount == 1 && model.data().Indices == fullIndices && !model.dirty(), "undo removes them");
				model.redo();
				context.check(model.data().Primitives[0].LodCount == 4 && model.data().Indices.size() > fullIndices.size(), "redo brings them back");
				context.check(model.clear_lods() && model.data().Primitives[0].LodCount == 1 && model.data().Indices == fullIndices, "clearing in the editor removes them");
				model.undo();
				context.check(model.data().Primitives[0].LodCount == 4, "and that can be undone");
				const LodBakeResult tiny = bake_mesh_lods(mesh, LodBakeSettings{ 3, 0.5f, 0.05f, 100000 });
				context.check(tiny.Levels == 0 && mesh.Indices == fullIndices, "a mesh under the triangle minimum gets none");
			});

		register_render_check("static_instance_has_no_motion", [](RenderCheckContext& context)
			{
				RenderScene scene;
				const EntityGuid movable = EntityGuid::generate();
				const EntityGuid fixed = EntityGuid::generate();
				const auto place = [&](const EntityGuid& guid, bool isStatic, float x)
				{
					RenderInstance instance;
					instance.Alive = true;
					instance.Static = isStatic;
					instance.World = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
					SceneDelta delta;
					delta.InstanceUpserts.emplace_back(guid, std::move(instance));
					scene.apply(std::move(delta));
				};
				place(movable, false, 0.0f);
				place(fixed, true, 0.0f);
				scene.end_frame();
				place(movable, false, 5.0f);
				place(fixed, true, 5.0f);

				const RenderInstance& moving = scene.instances()[*scene.find_instance(movable)];
				const RenderInstance& still = scene.instances()[*scene.find_instance(fixed)];
				context.check(moving.PrevWorld[3].x == 0.0f && moving.World[3].x == 5.0f, "a moving instance remembers where it was last frame");
				context.check(still.PrevWorld == still.World && still.World[3].x == 5.0f, "a static one has no previous matrix: it moved, with no motion");
				scene.end_frame();
				context.check(scene.instances()[*scene.find_instance(movable)].PrevWorld[3].x == 5.0f, "the moving one catches up at the end of the frame");
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
