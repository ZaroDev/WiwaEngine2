#include <pch.h>
#include "scene_tester.h"

#include "half_float.h"

#include <renderer/resources/gpu_resource_cache.h>

// Checks of skinning on the render thread: the palettes arrive with the scene deltas and follow the same previous-frame rule as the world
// matrix; a skinned mesh is deformed by its palette in the color and in the velocity; a mesh without a palette, and every rigid mesh, are
// drawn as they were.
namespace wiwa
{
	namespace
	{
		// The rows of a palette (three float4 per joint, rows of the 3x4 matrix) for joints that are translations.
		std::vector<glm::vec4> translations(const std::vector<glm::vec3>& offsets)
		{
			std::vector<glm::vec4> rows;
			for (const glm::vec3& offset : offsets)
			{
				rows.emplace_back(1.0f, 0.0f, 0.0f, offset.x);
				rows.emplace_back(0.0f, 1.0f, 0.0f, offset.y);
				rows.emplace_back(0.0f, 0.0f, 1.0f, offset.z);
			}
			return rows;
		}

		// A quad in the XY plane from x = -0.5 to 0.5 and y = -0.5 to 0.5, facing +Z. The vertices on the left belong to joint 0, those on
		// the right to joint 1: moving joint 1 stretches the quad to the right.
		MeshData make_skinned_quad(const AssetRef& material, bool withSkin = true)
		{
			MeshData mesh;
			mesh.Name = "skinned quad";
			const glm::vec2 corners[4] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
			for (const glm::vec2& corner : corners)
			{
				MeshVertex vertex;
				vertex.Position[0] = corner.x;
				vertex.Position[1] = corner.y;
				vertex.Position[2] = 0.0f;
				vertex.Normal[2] = 1.0f;
				vertex.Uv[0] = corner.x + 0.5f;
				vertex.Uv[1] = 0.5f - corner.y;
				mesh.Vertices.push_back(vertex);

				MeshSkinVertex skin;
				skin.Joints[0] = corner.x > 0.0f ? 1u : 0u;
				skin.Weights[0] = 1.0f;
				mesh.SkinVertices.push_back(skin);
			}
			mesh.Indices = { 0, 1, 2, 0, 2, 3 };

			MeshPrimitive primitive;
			primitive.IndexCount = 6;
			primitive.VertexCount = 4;
			primitive.Material = material;
			primitive.Skinned = withSkin;
			primitive.BoundsMin = glm::vec3(-0.5f, -0.5f, 0.0f);
			primitive.BoundsMax = glm::vec3(0.5f, 0.5f, 0.0f);
			mesh.Primitives.push_back(primitive);
			mesh.BoundsMin = primitive.BoundsMin;
			mesh.BoundsMax = primitive.BoundsMax;
			if (!withSkin)
			{
				mesh.SkinVertices.clear();
			}
			return mesh;
		}

		void set_palette(SceneTester& test, EntityGuid guid, std::vector<glm::vec4> rows)
		{
			SceneDelta delta;
			delta.PaletteUpdates.emplace_back(guid, std::move(rows));
			test.scene().apply(std::move(delta));
		}

		// Is the pixel at a world point on the quad (clearly brighter than the background in the corner of the image) rather than the background?
		bool lit(SceneTester& test, glm::vec3 point)
		{
			return SceneTester::luma(test.pixel(test.to_pixel(point))) > SceneTester::luma(test.pixel(2, test.size() - 3)) + 40;
		}

		struct LookCamera
		{
			glm::mat4 View;
			glm::mat4 Projection;
		};

		// uv of a world point for the tester's default camera, origin top left.
		glm::vec2 project_uv(glm::vec3 point)
		{
			const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			const glm::vec4 clip = projection * view * glm::vec4(point, 1.0f);
			return glm::vec2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
		}

		bool read_velocity(RenderCheckContext& context, SceneTester& test, glm::ivec2 pixel, glm::vec2& velocity)
		{
			std::vector<u8> bytes;
			u32 width = 0;
			u32 height = 0;
			if (!read_texture_slice_bytes(context.Device, test.renderer().get_viewport().velocity(), 0, 0, bytes, width, height, 4))
			{
				return false;
			}
			const size_t offset = (static_cast<size_t>(pixel.y) * width + static_cast<size_t>(pixel.x)) * 4;
			u16 raw[2];
			std::memcpy(raw, bytes.data() + offset, sizeof(raw));
			velocity = glm::vec2(half_to_float(raw[0]), half_to_float(raw[1]));
			return true;
		}

		// A scene with a rigid box first (so that the skinned quad does not start at vertex 0 of its page) and the skinned quad.
		struct SkinScene
		{
			EntityGuid Quad{};
			AssetRef QuadMesh{};
		};

		SkinScene build_skin_scene(SceneTester& test, bool quadHasSkin = true)
		{
			SkinScene scene;
			const AssetRef white = test.add_material(test.color_material(glm::vec3(1.0f), 1.0f));
			test.add_light(test.sun(glm::vec3(0.0f, 0.0f, -1.0f), 3.0f));
			// the rigid box sits out of the way, at the top of the image
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.2f), white)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.8f, 0.0f)));
			scene.QuadMesh = test.add_mesh(make_skinned_quad(white, quadHasSkin));
			scene.Quad = test.add_instance(scene.QuadMesh, glm::mat4(1.0f));
			return scene;
		}
	}

	void register_skinning_checks()
	{
		// The scene mirror only (no GPU): palettes arrive with a delta, the previous one follows end_frame like PrevWorld does.
		register_render_check("palette_previous_follows_end_frame", [](RenderCheckContext& context)
			{
				RenderScene scene;
				const EntityGuid guid{ 1, 2 };
				RenderInstance instance;
				instance.Mesh.Guid = AssetGuid::generate();
				SceneDelta first;
				first.InstanceUpserts.emplace_back(guid, std::move(instance));
				scene.apply(std::move(first));
				const size_t slot = *scene.find_instance(guid);
				const auto palette = [&]() -> const std::vector<glm::vec4>& { return scene.instances()[slot].Palette; };
				const auto previous = [&]() -> const std::vector<glm::vec4>& { return scene.instances()[slot].PrevPalette; };

				const std::vector<glm::vec4> a = translations({ glm::vec3(1.0f) });
				const std::vector<glm::vec4> b = translations({ glm::vec3(2.0f) });
				const std::vector<glm::vec4> c = translations({ glm::vec3(3.0f) });
				SceneDelta deltaA;
				deltaA.PaletteUpdates.emplace_back(guid, a);
				scene.apply(std::move(deltaA));
				context.check(palette() == a && previous() == a, "a palette that arrives first has no motion: the previous one is the same");

				scene.end_frame();
				SceneDelta deltaB;
				deltaB.PaletteUpdates.emplace_back(guid, b);
				scene.apply(std::move(deltaB));
				context.check(palette() == b && previous() == a, "the next palette keeps the last rendered one as the previous");

				SceneDelta deltaC;
				deltaC.PaletteUpdates.emplace_back(guid, c);
				scene.apply(std::move(deltaC));
				context.check(palette() == c && previous() == a, "two palettes in one rendered frame: the previous one is still that of the last rendered frame");

				scene.end_frame();
				context.check(previous() == c, "end_frame makes the palette the previous one");

				// the same instance, upserted again (it moved): the palettes stay
				RenderInstance moved;
				moved.Mesh = scene.instances()[slot].Mesh;
				moved.World = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
				SceneDelta upsert;
				upsert.InstanceUpserts.emplace_back(guid, std::move(moved));
				scene.apply(std::move(upsert));
				context.check(palette() == c && previous() == c, "an upsert of the instance keeps its palettes");

				// an empty palette makes it rigid again
				SceneDelta cleared;
				cleared.PaletteUpdates.emplace_back(guid, std::vector<glm::vec4>{});
				scene.apply(std::move(cleared));
				context.check(palette().empty() && previous().empty(), "an empty palette clears both");

				// a palette for an instance that does not exist is ignored
				SceneDelta stray;
				stray.PaletteUpdates.emplace_back(EntityGuid{ 9, 9 }, a);
				scene.apply(std::move(stray));
				context.check(scene.instance_count() == 1, "a palette for no instance changes nothing");
			});

		register_render_check("skinned_quad_follows_palette", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const SkinScene skin = build_skin_scene(test);
				context.check(test.render(), "render the scene without a palette");

				const GpuMesh* gpuMesh = test.cache().get_mesh(skin.QuadMesh);
				context.check(gpuMesh != nullptr && gpuMesh->Skinned && !gpuMesh->Primitives.empty() && gpuMesh->Primitives[0].BaseVertex > 0,
					"the skinned quad is uploaded as skinned, after another mesh on its page (a base vertex above 0)");
				context.check(lit(test, glm::vec3(0.0f, 0.0f, 0.0f)) && !lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "without a palette it is drawn as it is: x = 0.8 is empty");

				// joint 1 moves 0.6 to the right: the right edge goes from 0.5 to 1.1. The world matrix does not change.
				set_palette(test, skin.Quad, translations({ glm::vec3(0.0f), glm::vec3(0.6f, 0.0f, 0.0f) }));
				context.check(test.render(2), "render with the palette");
				context.check(lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "the palette stretched the quad over x = 0.8");
				context.check(lit(test, glm::vec3(-0.3f, 0.0f, 0.0f)), "and the left side stays");
				context.check(!lit(test, glm::vec3(1.4f, 0.0f, 0.0f)), "x = 1.4 is still empty");

				// back to the identity: the quad is as it was
				set_palette(test, skin.Quad, translations({ glm::vec3(0.0f), glm::vec3(0.0f) }));
				context.check(test.render(2) && !lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "an identity palette gives the quad back");

				// and an empty palette draws the mesh unskinned
				set_palette(test, skin.Quad, translations({ glm::vec3(0.0f), glm::vec3(0.6f, 0.0f, 0.0f) }));
				context.check(test.render(2) && lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "the palette is back");
				set_palette(test, skin.Quad, {});
				context.check(test.render(2) && !lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "an empty palette draws the quad as it is");
			});

		register_render_check("skinned_without_palette_equals_rigid", [](RenderCheckContext& context)
			{
				std::vector<u8> skinnedImage;
				{
					SceneTester test(context);
					build_skin_scene(test, true);
					context.check(test.render(), "render the skinned mesh without a palette");
					skinnedImage = test.pixels();
				}
				SceneTester test(context);
				build_skin_scene(test, false);
				context.check(test.render(), "render the same mesh without skin data");
				context.check(SceneTester::max_difference(skinnedImage, test.pixels()) == 0, "the two images are the same: edit mode shows the rest pose");
			});

		register_render_check("skinned_palette_too_short_is_ignored", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const SkinScene skin = build_skin_scene(test);
				// the mesh names joint 1 but the palette has one joint: reading past it must not happen, the mesh is drawn unskinned
				set_palette(test, skin.Quad, translations({ glm::vec3(0.0f, 0.0f, 0.0f) }));
				context.check(test.render(), "render with a palette that is too short");
				context.check(lit(test, glm::vec3(0.0f, 0.0f, 0.0f)) && !lit(test, glm::vec3(0.8f, 0.0f, 0.0f)), "the quad is drawn unskinned");
			});

		register_render_check("skinned_velocity_uses_previous_palette", [](RenderCheckContext& context)
			{
				const std::vector<glm::vec4> rest = translations({ glm::vec3(0.0f), glm::vec3(0.0f) });
				const std::vector<glm::vec4> moved = translations({ glm::vec3(0.0f), glm::vec3(0.5f, 0.0f, 0.0f) });

				// joint 1 moved 0.5 in the last frame: the point now at x = 0.7 was at x = -0.5 + (0.7 + 0.5) / 1.5 * 1.0 = 0.3
				glm::vec2 moving(0.0f);
				{
					SceneTester test(context);
					const SkinScene skin = build_skin_scene(test);
					set_palette(test, skin.Quad, rest);
					context.check(test.render(), "render the quad at rest");
					set_palette(test, skin.Quad, moved);
					context.check(test.frame(), "render the frame in which joint 1 moves");
					const glm::vec2 expected = project_uv(glm::vec3(0.7f, 0.0f, 0.0f)) - project_uv(glm::vec3(0.3f, 0.0f, 0.0f));
					context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.7f, 0.0f, 0.0f)), moving), "read the velocity");
					context.check(glm::distance(moving, expected) < 3e-3f && expected.x > 0.05f, "the velocity is the motion of the skinned point: current uv minus uv with the previous palette");
				}

				// negative control: the same final palette with no motion before it has no velocity, so the result does depend on the previous palette
				{
					SceneTester test(context);
					const SkinScene skin = build_skin_scene(test);
					set_palette(test, skin.Quad, moved);
					context.check(test.render(), "render the quad already moved");
					context.check(test.frame(), "render one more frame");
					glm::vec2 still(1.0f);
					context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.7f, 0.0f, 0.0f)), still), "read the velocity");
					context.check(glm::length(still) < 1e-4f && glm::distance(still, moving) > 0.05f, "without motion the velocity is zero and differs from the moving one");
				}
			});
	}
}
