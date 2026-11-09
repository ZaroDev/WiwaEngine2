#include <pch.h>
#include "render_test.h"

#include <scene/scene.h>

#include "../scene/render_scene.h"
#include "../scene/scene_extractor.h"

// Checks of the scene to render-thread path. They are plain logic (no GPU), run in the same list.
namespace wiwa
{
	namespace
	{
		AssetRef make_ref(u64 id)
		{
			AssetRef ref;
			ref.Guid = AssetGuid{ id, id * 7 + 1 };
			return ref;
		}

		entt::entity add_renderer(Scene& scene, const char* name, u64 meshId)
		{
			const entt::entity entity = scene.create(name);
			scene.registry().emplace<MeshRenderer>(entity, MeshRenderer{ make_ref(meshId), {} });
			return entity;
		}

		bool near_equal(float a, float b)
		{
			return std::abs(a - b) < 1e-4f;
		}
	}

	void register_scene_checks()
	{
		register_render_check("extract_deltas", [](RenderCheckContext& context)
			{
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity first = add_renderer(*scene, "first", 1);
				const entt::entity second = add_renderer(*scene, "second", 2);
				const entt::entity sun = scene->create("sun");
				scene->registry().emplace<Light>(sun, Light{ LightKind::Directional });
				const entt::entity eye = scene->create("eye");
				scene->registry().emplace<Camera>(eye, Camera{ ProjectionKind::Perspective, 60.0f, 0.1f, 100.0f, 10.0f, true });
				scene->update_world_transforms();

				SceneExtractor extractor;
				SceneDelta delta = extractor.extract(scene, 1.5f);
				context.check(delta.Reset, "the first extract resets");
				context.check(delta.InstanceUpserts.size() == 2, "two instances");
				context.check(delta.LightUpserts.size() == 1, "one light");
				context.check(delta.Camera.has_value(), "the primary camera");
				context.check(delta.InstanceRemovals.empty() && delta.LightRemovals.empty(), "nothing removed");

				context.check(extractor.extract(scene, 1.5f).empty(), "nothing changed gives an empty delta");

				scene->registry().get<Transform>(second).Position = glm::vec3(3.0f, 0.0f, 0.0f);
				scene->update_world_transforms();
				delta = extractor.extract(scene, 1.5f);
				context.check(!delta.Reset && delta.InstanceUpserts.size() == 1 && delta.LightUpserts.empty() && !delta.Camera,
					"moving one entity gives exactly that instance");
				if (delta.InstanceUpserts.size() == 1)
				{
					context.check(delta.InstanceUpserts[0].first == scene->registry().get<Id>(second).Guid, "the moved instance");
					context.check(delta.InstanceUpserts[0].second.World[3].x == 3.0f, "with its new matrix");
				}

				const EntityGuid firstGuid = scene->registry().get<Id>(first).Guid;
				scene->destroy(first);
				scene->update_world_transforms();
				delta = extractor.extract(scene, 1.5f);
				context.check(delta.InstanceRemovals.size() == 1 && delta.InstanceRemovals[0] == firstGuid && delta.InstanceUpserts.empty(),
					"destroying one gives exactly one removal");

				delta = extractor.extract(scene, 2.0f);
				context.check(delta.Camera.has_value() && delta.InstanceUpserts.empty(), "a new aspect resends the camera only");

				scene->registry().get<Camera>(eye).Primary = false;
				delta = extractor.extract(scene, 2.0f);
				context.check(delta.CameraRemoved, "no primary camera is reported");

				Ref<Scene> other = CreateRef<Scene>();
				add_renderer(*other, "other", 5);
				other->update_world_transforms();
				delta = extractor.extract(other, 2.0f);
				context.check(delta.Reset && delta.InstanceUpserts.size() == 1, "another scene resets");
			});

		register_render_check("render_scene_apply", [](RenderCheckContext& context)
			{
				RenderScene scene;
				const EntityGuid a{ 1, 1 };
				const EntityGuid b{ 2, 2 };
				const EntityGuid c{ 3, 3 };
				const EntityGuid d{ 4, 4 };

				SceneDelta delta;
				for (const EntityGuid& guid : { a, b, c })
				{
					RenderInstance instance;
					instance.Mesh = make_ref(guid.Hi);
					delta.InstanceUpserts.emplace_back(guid, std::move(instance));
				}
				scene.apply(std::move(delta));
				context.check(scene.instance_count() == 3, "three instances");
				const u64 revision = scene.revision();

				scene.apply(SceneDelta{});
				context.check(scene.revision() == revision, "an empty delta changes nothing");

				const std::optional<size_t> bSlot = scene.find_instance(b);
				context.check(bSlot.has_value(), "b has a slot");
				delta = SceneDelta{};
				delta.InstanceRemovals.push_back(b);
				scene.apply(std::move(delta));
				context.check(scene.instance_count() == 2 && !scene.find_instance(b), "b removed");
				context.check(bSlot && !scene.instances()[*bSlot].Alive, "its slot is dead");

				delta = SceneDelta{};
				RenderInstance instance;
				instance.Mesh = make_ref(4);
				delta.InstanceUpserts.emplace_back(d, std::move(instance));
				scene.apply(std::move(delta));
				context.check(scene.instance_count() == 3 && scene.instances().size() == 3, "the freed slot is reused");
				context.check(bSlot && scene.find_instance(d) == bSlot, "d took b slot");
				context.check(scene.revision() > revision, "the revision moved on");

				delta = SceneDelta{};
				delta.Camera = RenderCamera{};
				scene.apply(std::move(delta));
				context.check(scene.camera().has_value(), "a camera");

				delta = SceneDelta{};
				delta.Reset = true;
				scene.apply(std::move(delta));
				context.check(scene.instance_count() == 0 && scene.instances().empty() && !scene.camera() && scene.light_count() == 0,
					"Reset clears everything");
			});

		register_render_check("decal_extract_and_apply", [](RenderCheckContext& context)
			{
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity stain = scene->create("stain");
				Decal decal;
				decal.Material = make_ref(9);
				decal.Size = glm::vec3(2.0f, 3.0f, 0.5f);
				decal.Opacity = 0.5f;
				decal.AffectNormal = false;
				decal.SortOrder = 4;
				scene->registry().emplace<Decal>(stain, decal);
				const entt::entity bare = scene->create("no material");
				scene->registry().emplace<Decal>(bare, Decal{});
				scene->update_world_transforms();

				SceneExtractor extractor;
				SceneDelta delta = extractor.extract(scene, 1.0f);
				context.check(delta.DecalUpserts.size() == 2, "both decals are sent, also the one without a material");
				context.check(!delta.empty() && extractor.extract(scene, 1.0f).empty(), "an unchanged decal gives an empty delta");

				RenderScene render;
				render.apply(std::move(delta));
				const EntityGuid stainGuid = scene->registry().get<Id>(stain).Guid;
				const EntityGuid bareGuid = scene->registry().get<Id>(bare).Guid;
				const std::optional<size_t> slot = render.find_decal(stainGuid);
				context.check(render.decal_count() == 2 && slot.has_value(), "the render scene holds both");
				if (slot)
				{
					const RenderDecal& stored = render.decals()[*slot];
					context.check(stored.Alive && stored.Material == make_ref(9) && stored.Size == glm::vec3(2.0f, 3.0f, 0.5f) && near_equal(stored.Opacity, 0.5f)
						&& stored.SortOrder == 4, "the fields arrive");
					context.check((stored.Channels & DECAL_CHANNEL_NORMAL) == 0 && (stored.Channels & DECAL_CHANNEL_COLOR) != 0, "the channel toggles become bits");
				}

				scene->registry().get<Transform>(stain).Position = glm::vec3(1.0f, 0.0f, 0.0f);
				scene->update_world_transforms();
				delta = extractor.extract(scene, 1.0f);
				context.check(delta.DecalUpserts.size() == 1 && delta.DecalUpserts[0].first == stainGuid && delta.DecalUpserts[0].second.World[3].x == 1.0f,
					"moving a decal sends exactly that decal");
				render.apply(std::move(delta));

				scene->registry().get<Decal>(stain).Material = make_ref(10);
				delta = extractor.extract(scene, 1.0f);
				context.check(delta.DecalUpserts.size() == 1, "changing the material sends the decal again");
				render.apply(std::move(delta));
				context.check(slot && render.find_decal(stainGuid) == slot && render.decals()[*slot].Material == make_ref(10), "the same slot holds the new material");

				scene->destroy(bare);
				scene->update_world_transforms();
				delta = extractor.extract(scene, 1.0f);
				context.check(delta.DecalRemovals.size() == 1 && delta.DecalRemovals[0] == bareGuid && delta.DecalUpserts.empty(), "destroying a decal sends one removal");
				const std::optional<size_t> bareSlot = render.find_decal(bareGuid);
				render.apply(std::move(delta));
				context.check(render.decal_count() == 1 && !render.find_decal(bareGuid) && bareSlot && !render.decals()[*bareSlot].Alive, "its slot is dead");

				SceneDelta onlyDecal;
				onlyDecal.DecalRemovals.push_back(stainGuid);
				context.check(!onlyDecal.empty(), "a delta with only a decal change is not empty");
				render.apply(std::move(onlyDecal));

				const EntityGuid again{ 77, 77 };
				SceneDelta add;
				add.DecalUpserts.emplace_back(again, RenderDecal{});
				render.apply(std::move(add));
				context.check(render.decal_count() == 1 && slot && render.find_decal(again) == slot, "the most recently freed slot is reused");

				SceneDelta reset;
				reset.Reset = true;
				render.apply(std::move(reset));
				context.check(render.decal_count() == 0 && render.decals().empty(), "Reset clears the decals");
			});

		register_render_check("directional_direction", [](RenderCheckContext& context)
			{
				Ref<Scene> scene = CreateRef<Scene>();
				const entt::entity sun = scene->create("sun");
				scene->registry().emplace<Light>(sun, Light{ LightKind::Directional });
				scene->registry().get<Transform>(sun).Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				scene->update_world_transforms();

				SceneExtractor extractor;
				const SceneDelta delta = extractor.extract(scene, 1.0f);
				context.check(delta.LightUpserts.size() == 1, "one light");
				if (delta.LightUpserts.size() == 1)
				{
					const glm::vec3 direction = delta.LightUpserts[0].second.Direction;
					context.check(near_equal(direction.x, -1.0f) && near_equal(direction.y, 0.0f) && near_equal(direction.z, 0.0f),
						"a light turned 90 degrees about Y points along -X");
				}
			});
	}
}
