#include <pch.h>
#include "render_test.h"

#include "../render_pick.h"
#include "scene_tester.h"

// Checks of GPU picking: the entity under a pixel, pick icons, the nearest of overlapping entities, the ID target's lifetime and
// requests that cannot be answered (outside the viewport, a resize, a scene reset, a newer request).
namespace wiwa
{
	namespace
	{
		constexpr u32 SIZE = 64;

		// Routes the renderer's pick results to a channel of the check for as long as it lives.
		class PickProbe
		{
		public:
			explicit PickProbe(SceneTester& test) : m_Test(test), m_Previous(test.renderer().get_pick().Channel)
			{
				test.renderer().set_pick_channel(&m_Channel);
			}

			~PickProbe() { m_Test.renderer().set_pick_channel(m_Previous); }

			// Asks for the pixel and renders one frame; the answer (empty when nothing answered yet) is what the frame published.
			std::optional<PickResult> pick(u32 x, u32 y)
			{
				m_Test.renderer().set_pick_request(PickRequest{ ++m_NextId, x, y });
				m_Test.frame();
				return m_Channel.take();
			}

			PickChannel& channel() { return m_Channel; }
			u64 last_id() const { return m_NextId; }

		private:
			SceneTester& m_Test;
			PickChannel* m_Previous;
			PickChannel m_Channel{};
			u64 m_NextId = 0;
		};

		void dark_scene(SceneTester& test)
		{
			SceneSettings settings;
			settings.AmbientIntensity = 1.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			test.scene().set_settings(settings);
		}

		AssetRef box(SceneTester& test, float half)
		{
			return test.add_mesh(make_test_box(glm::vec3(half), test.add_material(test.color_material(glm::vec3(0.5f)))));
		}

		bool answered(const std::optional<PickResult>& result, u64 id) { return result && result->RequestId == id; }
		bool picked(const std::optional<PickResult>& result, u64 id, EntityGuid guid) { return answered(result, id) && result->Guid && *result->Guid == guid; }
		bool picked_nothing(const std::optional<PickResult>& result, u64 id) { return answered(result, id) && !result->Guid; }
	}

	void register_pick_checks()
	{
		register_render_check("pick_entity", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				const EntityGuid left = test.add_instance(box(test, 0.5f), glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f)));
				const EntityGuid right = test.add_instance(box(test, 0.5f), glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f)));
				context.check(test.render(), "render the scene");

				PickProbe probe(test);
				const glm::ivec2 leftPixel = test.to_pixel(glm::vec3(-1.5f, 0.0f, 0.5f));
				const glm::ivec2 rightPixel = test.to_pixel(glm::vec3(1.5f, 0.0f, 0.5f));
				context.check(picked(probe.pick(leftPixel.x, leftPixel.y), probe.last_id() + 1, left), "a click on the left cube picks it");
				context.check(picked(probe.pick(rightPixel.x, rightPixel.y), probe.last_id() + 1, right), "a click on the right cube picks it");
				context.check(picked_nothing(probe.pick(32, 32), probe.last_id() + 1), "a click between the cubes picks nothing");
				context.check(picked_nothing(probe.pick(1, 1), probe.last_id() + 1), "a click in the corner picks nothing");
			});

		register_render_check("pick_nearest", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				const EntityGuid back = test.add_instance(box(test, 1.0f), glm::mat4(1.0f));
				const EntityGuid front = test.add_instance(box(test, 0.3f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.0f)));
				context.check(test.render(), "render the scene");

				PickProbe probe(test);
				context.check(picked(probe.pick(32, 32), probe.last_id() + 1, front), "where they overlap the nearer one is picked");
				const glm::ivec2 rim = test.to_pixel(glm::vec3(0.8f, 0.0f, 1.0f));
				context.check(picked(probe.pick(rim.x, rim.y), probe.last_id() + 1, back), "beside the nearer one the farther one is picked");
			});

		register_render_check("pick_icon", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				const EntityGuid cube = test.add_instance(box(test, 0.5f), glm::mat4(1.0f));
				const EntityGuid light{ 0x11C0, 1 };
				const EntityGuid hidden{ 0x11C0, 2 };
				context.check(test.render(), "render the scene");

				PickProbe probe(test);
				const glm::vec3 above(0.0f, 1.6f, 0.0f);
				const glm::ivec2 abovePixel = test.to_pixel(above);
				test.debug().pick_icon(light, above, 10.0f);
				context.check(picked(probe.pick(abovePixel.x, abovePixel.y), probe.last_id() + 1, light), "a click on an icon picks its entity");
				context.check(picked(probe.pick(abovePixel.x + 8, abovePixel.y), probe.last_id() + 1, light) == false, "the icon is only there in the frame that registered it");

				test.debug().pick_icon(light, above, 10.0f);
				context.check(picked_nothing(probe.pick(abovePixel.x + 14, abovePixel.y), probe.last_id() + 1), "outside the icon's radius nothing is picked");

				test.debug().pick_icon(hidden, glm::vec3(0.0f, 0.0f, -3.0f), 12.0f);
				context.check(picked(probe.pick(32, 32), probe.last_id() + 1, cube), "an icon behind a cube does not take the click");
			});

		register_render_check("pick_released", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.add_instance(box(test, 0.5f), glm::mat4(1.0f));
				context.check(test.render(), "render the scene");

				PickProbe probe(test);
				context.check(!test.renderer().get_pick().TargetAlive, "no ID target before a pick");
				context.check(answered(probe.pick(32, 32), probe.last_id() + 1), "a pick is answered");
				context.check(!test.renderer().get_pick().TargetAlive, "the ID target is released after the pick");
				test.frame();
				context.check(!test.renderer().get_pick().TargetAlive && !probe.channel().take(), "frames without a request make no target and no answer");
			});

		register_render_check("pick_stale", [](RenderCheckContext& context)
			{
				PickChannel channel;
				channel.publish(PickResult{ 1, std::nullopt });
				context.check(!channel.take(2), "a result older than the newest request is dropped");
				context.check(!channel.take(), "and it does not come back");
				channel.publish(PickResult{ 3, EntityGuid{ 5, 6 } });
				const std::optional<PickResult> fresh = channel.take(2);
				context.check(fresh && fresh->RequestId == 3 && fresh->Guid && *fresh->Guid == EntityGuid({ 5, 6 }), "a newer result is taken");

				channel.publish(PickResult{ 4, std::nullopt });
				channel.publish(PickResult{ 5, std::nullopt });
				const std::optional<PickResult> latest = channel.take();
				context.check(latest && latest->RequestId == 5 && !channel.take(), "the latest result wins");

				SceneTester test(context, SIZE);
				dark_scene(test);
				const EntityGuid cube = test.add_instance(box(test, 0.5f), glm::mat4(1.0f));
				context.check(test.render(), "render the scene");
				PickProbe probe(test);
				test.renderer().set_pick_request(PickRequest{ 100, 1, 1 });
				test.renderer().set_pick_request(PickRequest{ 101, 32, 32 });
				test.frame();
				const std::optional<PickResult> second = probe.channel().take();
				context.check(picked(second, 101, cube), "only the newest of two requests is answered");
				context.check(!probe.channel().take(), "the replaced request is never answered");
			});

		register_render_check("pick_unanswerable", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.add_instance(box(test, 0.5f), glm::mat4(1.0f));
				context.check(test.render(), "render the scene");
				const u32 errorsBefore = get_error_message_count();

				PickProbe probe(test);
				context.check(picked_nothing(probe.pick(500, 500), probe.last_id() + 1), "a pixel outside the viewport picks nothing");
				context.check(picked_nothing(probe.pick(SIZE, SIZE), probe.last_id() + 1), "the pixel just outside picks nothing");

				// A resize between the click and the frame: the pixel is outside the new, smaller viewport.
				test.renderer().set_pick_request(PickRequest{ 200, 40, 40 });
				test.set_size(1);
				test.frame();
				test.frame();
				const std::optional<PickResult> resized = probe.channel().take();
				context.check(picked_nothing(resized, 200), "a click outside a viewport that shrank picks nothing");

				test.set_size(SIZE);
				context.check(test.render(), "render at the original size");

				// The scene is replaced between the click and the frame.
				test.renderer().set_pick_request(PickRequest{ 300, 32, 32 });
				SceneDelta reset;
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				test.frame();
				context.check(picked_nothing(probe.channel().take(), 300), "after a scene reset the old entity is not picked");
				context.check(get_error_message_count() == errorsBefore, "no GPU validation messages");
				context.check(!test.renderer().get_pick().TargetAlive, "the target is released");
			});
	}
}
