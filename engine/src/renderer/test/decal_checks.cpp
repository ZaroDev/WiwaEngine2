#include <pch.h>
#include "scene_tester.h"

#include "cross_backend.h"

// Checks of the decals (decals.md): the decal pass draws the box of a decal into the decal targets and the forward shaders lay them over
// the surface. A grey plane facing +Z at z = 0.05 seen from (0, 0, 5); a decal projects along its local -Z, so an unrotated box over
// the plane projects onto it.
namespace wiwa
{
	namespace
	{
		struct DecalScene
		{
			explicit DecalScene(SceneTester& test)
			{
				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.3f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(3.0f, 3.0f, 0.05f), material)), glm::mat4(1.0f));
				test.add_light(test.sun(glm::vec3(0.0f, -0.3f, -1.0f), 2.0f));
			}
		};

		RenderDecal make_decal(const AssetRef& material, glm::vec3 size, const glm::mat4& world = glm::mat4(1.0f))
		{
			RenderDecal decal;
			decal.World = world;
			decal.Material = material;
			decal.Size = size;
			return decal;
		}

		AssetRef color_decal(SceneTester& test, glm::vec3 color)
		{
			return test.add_material(test.color_material(color));
		}

		bool same_pixel(const glm::ivec3& a, const glm::ivec3& b, int tolerance = 1)
		{
			return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::ivec3(tolerance)));
		}

		glm::ivec3 on_plane(const SceneTester& test, float x, float y)
		{
			return test.pixel(test.to_pixel(glm::vec3(x, y, 0.05f)));
		}
	}

	void register_decal_checks()
	{
		register_render_check("decal_color_box", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				const std::vector<u8> before = test.pixels();
				const glm::ivec3 centerBefore = on_plane(test, 0.0f, 0.0f);
				const glm::ivec3 outsideBefore = on_plane(test, 1.0f, 0.0f);

				test.add_decal(make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 1.0f, 2.0f)));
				context.check(test.render(), "render with a red decal");
				const glm::ivec3 center = on_plane(test, 0.0f, 0.0f);
				WI_LOG(Info, "RenderTest", "decal_color_box center {0},{1},{2} before {3},{4},{5}", center.r, center.g, center.b, centerBefore.r, centerBefore.g, centerBefore.b);
				context.check(center.r > centerBefore.r + 20 && center.r > center.g * 2 && center.r > center.b * 2, "the pixel inside the box turns red");
				context.check(same_pixel(on_plane(test, 1.0f, 0.0f), outsideBefore), "a pixel outside the box is as it was");
				context.check(test.max_difference(before, test.pixels()) > 20, "the decal changed the image");
				compare_across_apis(context, "decal_color_box", test.pixels());
			});

		register_render_check("decal_none_visible", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				const std::vector<u8> before = test.pixels();

				const AssetRef red = color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f));
				// behind the camera, so culled; invisible (opacity 0); without a material; every channel off
				test.add_decal(make_decal(red, glm::vec3(1.0f, 1.0f, 1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 50.0f))));
				RenderDecal transparent = make_decal(red, glm::vec3(1.0f, 1.0f, 2.0f));
				transparent.Opacity = 0.0f;
				test.add_decal(transparent);
				test.add_decal(make_decal(AssetRef{}, glm::vec3(1.0f, 1.0f, 2.0f)));
				RenderDecal off = make_decal(red, glm::vec3(1.0f, 1.0f, 2.0f));
				off.Channels = 0;
				test.add_decal(off);
				context.check(test.render(), "render with decals that draw nothing");
				context.check(SceneTester::max_difference(before, test.pixels()) == 0, "they leave every pixel as it was, exactly");
				context.check(!test.renderer().get_scene_data().decals_active(), "and no decal targets are in use");
			});

		register_render_check("decal_camera_inside", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				test.set_camera(glm::vec3(0.0f, 0.0f, 0.6f), glm::vec3(0.0f, 0.0f, 0.0f));
				context.check(test.render(), "render without a decal");
				const glm::ivec3 before = on_plane(test, 0.0f, 0.0f);

				test.add_decal(make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(2.0f, 2.0f, 2.0f)));
				context.check(test.render(), "render from inside the box");
				const glm::ivec3 after = on_plane(test, 0.0f, 0.0f);
				context.check(after.r > before.r + 20 && after.r > after.g * 2, "the decal still draws when the camera is inside its box");
			});

		register_render_check("decal_sort_order", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				const AssetRef red = color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f));
				const AssetRef green = color_decal(test, glm::vec3(0.0f, 1.0f, 0.0f));

				RenderDecal low = make_decal(red, glm::vec3(1.0f, 1.0f, 2.0f));
				low.SortOrder = 1;
				RenderDecal high = make_decal(green, glm::vec3(1.0f, 1.0f, 2.0f));
				high.SortOrder = 2;
				const EntityGuid a = test.add_decal(low);
				const EntityGuid b = test.add_decal(high);
				context.check(test.render(), "render red under green");
				glm::ivec3 center = on_plane(test, 0.0f, 0.0f);
				context.check(center.g > center.r * 2, "the higher SortOrder wins the overlap (green)");

				test.remove_decal(a);
				test.remove_decal(b);
				low.SortOrder = 3;
				test.add_decal(low);
				test.add_decal(high);
				context.check(test.render(), "render green under red");
				center = on_plane(test, 0.0f, 0.0f);
				context.check(center.r > center.g * 2, "and with the orders swapped, red wins");
			});

		register_render_check("decal_channels", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				const glm::ivec3 before = on_plane(test, 0.0f, 0.0f);

				// color off: a red decal changes nothing else
				RenderDecal decal = make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 1.0f, 2.0f));
				decal.Channels = DECAL_CHANNEL_NORMAL;
				const EntityGuid colorOff = test.add_decal(decal);
				context.check(test.render(), "render with the color channel off");
				context.check(same_pixel(on_plane(test, 0.0f, 0.0f), before), "a decal without its color channel does not tint");
				test.remove_decal(colorOff);

				// emissive only: a green glow on the grey plane
				MaterialData glow = test.color_material(glm::vec3(0.0f));
				glow.EmissiveFactor = glm::vec3(0.0f, 3.0f, 0.0f);
				RenderDecal emissive = make_decal(test.add_material(glow), glm::vec3(1.0f, 1.0f, 2.0f));
				emissive.Channels = DECAL_CHANNEL_EMISSIVE;
				test.add_decal(emissive);
				context.check(test.render(), "render with an emissive decal");
				const glm::ivec3 lit = on_plane(test, 0.0f, 0.0f);
				context.check(lit.g > before.g + 30 && lit.r <= before.r + 2, "the emissive channel adds light (green) and nothing else");
			});

		register_render_check("decal_orm", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				const glm::ivec3 before = on_plane(test, 0.0f, 0.0f);

				// a fully metallic ORM: the diffuse light of the grey plane is gone under it
				MaterialData metal = test.color_material(glm::vec3(1.0f), 0.2f, 1.0f);
				RenderDecal decal = make_decal(test.add_material(metal), glm::vec3(1.0f, 1.0f, 2.0f));
				decal.Channels = DECAL_CHANNEL_ORM;
				test.add_decal(decal);
				context.check(test.render(), "render with a metallic ORM decal");
				const glm::ivec3 after = on_plane(test, 0.0f, 0.0f);
				context.check(SceneTester::luma(after) < SceneTester::luma(before) - 10, "the ORM channel changes how the surface shades");
			});

		register_render_check("decal_angle_fade", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				const glm::ivec3 before = on_plane(test, 0.0f, 0.0f);

				// a box that projects along Y: the plane (normal +Z) is edge-on to it
				const glm::mat4 turned = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
				test.add_decal(make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(2.0f, 2.0f, 2.0f), turned));
				context.check(test.render(), "render a decal projecting along the plane");
				context.check(same_pixel(on_plane(test, 0.0f, 0.0f), before), "a surface turned away from the projection does not get the decal");
			});

		register_render_check("decal_receives_opt_out", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				test.add_light(test.sun(glm::vec3(0.0f, -0.3f, -1.0f), 2.0f));
				const AssetRef grey = test.add_material(test.color_material(glm::vec3(0.3f)));
				const AssetRef plate = test.add_mesh(make_test_box(glm::vec3(0.7f, 1.5f, 0.05f), grey));
				test.add_instance(plate, glm::translate(glm::mat4(1.0f), glm::vec3(-0.8f, 0.0f, 0.0f)));
				const EntityGuid right = test.add_instance(plate, glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 0.0f, 0.0f)));
				context.check(test.render(), "render without a decal");
				const glm::ivec3 leftBefore = on_plane(test, -0.8f, 0.0f);
				const glm::ivec3 rightBefore = on_plane(test, 0.8f, 0.0f);

				test.add_decal(make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(3.0f, 1.0f, 2.0f)));
				context.check(test.render(), "render with a decal over both plates");
				const glm::ivec3 left = on_plane(test, -0.8f, 0.0f);
				context.check(left.r > leftBefore.r + 20 && left.r > left.g * 2, "the plate that receives decals turns red");
				context.check(on_plane(test, 0.8f, 0.0f).r > rightBefore.r + 20, "so does the other one while it receives decals too");

				// the right plate stops receiving decals
				const std::optional<size_t> slot = test.scene().find_instance(right);
				context.check(slot.has_value(), "the right plate is in the scene");
				if (slot)
				{
					RenderInstance copy = test.scene().instances()[*slot];
					copy.ReceivesDecals = false;
					SceneDelta delta;
					delta.InstanceUpserts.emplace_back(right, std::move(copy));
					test.scene().apply(std::move(delta));
				}
				context.check(test.render(), "render with the right plate opted out");
				context.check(same_pixel(on_plane(test, 0.8f, 0.0f), rightBefore), "the plate that does not receive decals is as it was");
				const glm::ivec3 leftAgain = on_plane(test, -0.8f, 0.0f);
				context.check(leftAgain.r > leftBefore.r + 20, "and the other plate still gets the decal");
			});

		register_render_check("decal_cap", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				const AssetRef red = color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f));
				const u32 count = SceneRenderData::MAX_VISIBLE_DECALS + 88;
				for (u32 i = 0; i < count; i++)
				{
					const float x = -1.2f + 2.4f * static_cast<float>(i % 30) / 30.0f;
					const float y = -1.2f + 2.4f * static_cast<float>(i / 30 % 30) / 30.0f;
					test.add_decal(make_decal(red, glm::vec3(0.05f, 0.05f, 2.0f), glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f))));
				}
				context.check(test.render(), "render more decals than the cap");
				const SceneRenderData& data = test.renderer().get_scene_data();
				context.check(data.visible_decals().size() == SceneRenderData::MAX_VISIBLE_DECALS, "at most the cap is drawn");
				context.check(data.dropped_decals() == 88, "the rest is counted as dropped");
			});

		register_render_check("decal_targets_follow_visibility", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				DecalScene scene(test);
				context.check(test.render(), "render without a decal");
				context.check(!test.renderer().get_scene_data().decals_active(), "no decal targets without decals");

				const EntityGuid decal = test.add_decal(make_decal(color_decal(test, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 1.0f, 2.0f)));
				context.check(test.render(), "render with a decal");
				context.check(test.renderer().get_scene_data().decals_active() && test.renderer().get_scene_data().viewport()->has_decal_targets(), "the targets are made");

				test.remove_decal(decal);
				context.check(test.render(), "render after removing it");
				context.check(!test.renderer().get_scene_data().decals_active(), "the forward shaders are given the stand-ins again");
				const std::vector<u8> after = test.pixels();
				SceneTester again(context);
				DecalScene sceneAgain(again);
				context.check(again.render(), "render the plain scene afresh");
				context.check(SceneTester::max_difference(after, again.pixels()) == 0, "the image is exactly the one without decals");
			});
	}
}
