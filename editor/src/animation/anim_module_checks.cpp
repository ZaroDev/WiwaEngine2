#include <editor_common.h>
#include "anim_check.h"
#include "anim_fixture.h"

#include <animation/animation_module.h>
#include <renderer/scene/scene_extractor.h>
#include <scene/prefab_expander.h>
#include <scene/scene_clone.h>

namespace wiwa
{
	namespace
	{
		bool near_equal(float a, float b, float epsilon = 1e-4f)
		{
			return std::abs(a - b) < epsilon;
		}

		float hip_x(const AnimatorInstance& instance)
		{
			return instance.pose().T[0].x;
		}

		void prefab_and_components(AnimCheck& check)
		{
			Fixture fixture;

			// a model with animations: the root gets an Animator, a skinned node a SkinBinding
			PrefabData prefab;
			prefab.Name = "model";
			PrefabNode body;
			body.Name = "body";
			body.StableId = 11;
			body.Skeleton = fixture.SkeletonRef;
			PrefabNode other;
			other.Name = "other";
			other.StableId = 12;
			prefab.Nodes = { body, other };
			prefab.Animations = { fixture.IdleRef, fixture.RunRef };

			EntityRecord root;
			root.Id = EntityGuid::generate();
			const ExpandResult expanded = expand_prefab(root, prefab);
			auto has = [](const EntityRecord& record, const char* name)
			{
				return std::any_of(record.Components.begin(), record.Components.end(), [&](const ComponentRecord& c) { return c.Name == name; });
			};
			check(expanded.Entities.size() == 3 && has(expanded.Entities[0], "Animator"), "the root of a model with animations gets an Animator");
			check(has(expanded.Entities[1], "SkinBinding") && !has(expanded.Entities[2], "SkinBinding"), "a skinned node gets a SkinBinding and a rigid one does not");

			PrefabData rigid = prefab;
			rigid.Animations.clear();
			check(!has(expand_prefab(root, rigid).Entities[0], "Animator"), "a model without animations gets no Animator");

			EntityRecord authored = root;
			authored.Components.push_back(ComponentRecord{ "Animator", { FieldRecord{ "Speed", FieldValue(2.0f) } } });
			const ExpandResult kept = expand_prefab(authored, prefab);
			check(std::count_if(kept.Entities[0].Components.begin(), kept.Entities[0].Components.end(), [](const ComponentRecord& c) { return c.Name == "Animator"; }) == 1,
				"an Animator the scene already has is not added again");

			// the components survive a copy of the scene (play mode runs on one)
			Scene scene;
			const entt::entity hero = fixture.make_character(scene);
			scene.registry().get<Animator>(hero).Speed = 1.5f;
			const entt::entity socketEntity = scene.create("hand_prop", hero);
			scene.registry().emplace<AnimationSocket>(socketEntity, AnimationSocket{ "hand", glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
			scene.registry().emplace<SkinBinding>(socketEntity, SkinBinding{ fixture.SkeletonRef });
			const Ref<Scene> copy = clone_scene(scene);
			const entt::entity copiedHero = copy->find(scene.registry().get<Id>(hero).Guid);
			const entt::entity copiedSocket = copy->find(scene.registry().get<Id>(socketEntity).Guid);
			const Animator* copiedAnimator = copy->registry().try_get<Animator>(copiedHero);
			check(copiedAnimator != nullptr && near_equal(copiedAnimator->Speed, 1.5f) && copiedAnimator->Clips.size() == 4 && copiedAnimator->Clip == fixture.IdleRef, "Animator is copied with the scene");
			const AnimationSocket* copiedSocketComponent = copy->registry().try_get<AnimationSocket>(copiedSocket);
			check(copiedSocketComponent != nullptr && copiedSocketComponent->Joint == "hand" && near_equal(copiedSocketComponent->OffsetPosition.y, 1.0f), "AnimationSocket is copied with the scene");
			check(copy->registry().all_of<SkinBinding>(copiedSocket), "SkinBinding is copied with the scene");
		}

		void module_playback(AnimCheck& check)
		{
			Fixture fixture;
			AnimationModule module;
			module.set_sources(fixture.Sources);
			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity hero = fixture.make_character(*scene);

			module.tick(scene, 0.1f, false);
			check(module.instance_count() == 0, "nothing is made while the game does not play");

			module.tick(scene, 0.1f, true);
			AnimatorInstance* instance = module.instance_for(hero);
			check(instance != nullptr && instance->ready() && near_equal(hip_x(*instance), 1.0f), "PlayOnStart plays the Clip");
			check(instance->palette().size() == 2, "the palette has a matrix per joint");

			instance->play({ "Run", {} });
			module.tick(scene, 0.25f, true);
			check(near_equal(hip_x(*instance), 2.5f), "play by name starts the clip: a quarter of a second is x = 2.5");
			check(module.take_events().empty(), "no event before its time");
			module.tick(scene, 0.5f, true);
			const std::vector<QueuedAnimationEvent> events = module.take_events();
			check(events.size() == 1 && events[0].Event.Name == "footstep" && events[0].Animator == scene->registry().get<Id>(hero).Guid, "the clip event is queued with the animator that fired it");
			check(module.take_events().empty(), "events are taken once");

			// Speed on the component scales the time
			scene->registry().get<Animator>(hero).Speed = 2.0f;
			instance->play({ "Run", {} }, { true, 1.0f });
			module.tick(scene, 0.25f, true);
			check(near_equal(hip_x(*instance), 5.0f), "the Animator speed scales time");

			// a game that is paused keeps its pose
			const float before = hip_x(*instance);
			module.tick(scene, 0.25f, false);
			check(near_equal(hip_x(*instance), before), "a paused game does not advance the pose");

			// a disabled Animator and a destroyed entity end their instances
			scene->registry().get<Animator>(hero).Enabled = false;
			module.tick(scene, 0.1f, true);
			check(module.instance_for(hero) == nullptr, "a disabled Animator has no instance");
			scene->registry().get<Animator>(hero).Enabled = true;
			module.tick(scene, 0.1f, true);
			check(module.instance_for(hero) != nullptr, "enabling it makes a new one");
			scene->destroy(hero);
			module.tick(scene, 0.1f, true);
			check(module.instance_count() == 0, "a destroyed entity ends its instance");

			// another scene object (play mode's copy) starts with none
			Ref<Scene> second = CreateRef<Scene>();
			fixture.make_character(*second);
			module.tick(second, 0.1f, true);
			check(module.instance_count() == 1, "an instance is made for the new scene");
			Ref<Scene> copy = clone_scene(*second);
			module.tick(copy, 0.1f, true);
			AnimatorInstance* fresh = module.instance_for(*copy->registry().view<Animator>().begin());
			check(fresh != nullptr && module.instance_count() == 1 && near_equal(fresh->time(), 0.1f), "a scene clone starts with fresh instances (one tick old, not two)");
		}

		void previews(AnimCheck& check)
		{
			Fixture fixture;
			AnimationModule module;
			module.set_sources(fixture.Sources);
			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity hero = fixture.make_character(*scene);

			module.tick(scene, 0.1f, false);
			check(module.preview(hero) != nullptr && module.preview(scene->create("plain")) == nullptr, "a preview exists for an entity with an Animator only");
			AnimatorInstance* preview = module.preview(hero);
			preview->play({ "Run", {} });
			module.tick(scene, 0.25f, false);
			check(near_equal(hip_x(*preview), 2.5f), "a preview advances while the game does not play");
			module.set_preview_paused(hero, true);
			module.tick(scene, 0.25f, false);
			check(near_equal(hip_x(*preview), 2.5f), "a paused preview holds its pose");
			preview->seek(0.5f);
			module.tick(scene, 0.0f, false);
			check(near_equal(hip_x(*preview), 5.0f), "and follows a scrub");

			module.tick(scene, 0.1f, true);
			module.drop_previews();
			check(module.instance_for(hero) == nullptr, "dropping the previews ends them");

			// an instance the game made is not a preview
			module.tick(scene, 0.1f, true);
			check(module.instance_for(hero) != nullptr, "the game makes its own");
			module.drop_previews();
			check(module.instance_for(hero) != nullptr, "and dropping previews leaves it");
		}

		void play_tree(AnimCheck& check)
		{
			Fixture fixture;
			AnimatorInstance instance(fixture.Sources, fixture.config());
			std::vector<ClipEvent> events;

			// a clip that has not loaded is held and starts the frame it can
			instance.play({ "Late", {} });
			instance.update(0.1f, events);
			check(instance.pending_commands() == 1 && near_equal(hip_x(instance), 0.0f), "a clip that has not loaded waits");
			fixture.LateLoaded = true;
			instance.update(0.1f, events);
			check(instance.pending_commands() == 0 && near_equal(hip_x(instance), 7.0f), "it starts the frame it is ready");

			AssetRef unused;
			check(instance.lookup("Nope", unused) == ClipLookup::Unknown && instance.lookup("Run", unused) == ClipLookup::Found && unused == fixture.RunRef, "lookup by name");
			check(instance.clip_names().size() == 4, "the loaded clips are listed");

			// a crossfade: halfway, the pose is halfway
			instance.play({ "Idle", {} });
			instance.crossfade({ "Run", {} }, 1.0f, { true, 0.0f });
			instance.update(0.5f, events);
			check(near_equal(hip_x(instance), 0.5f * 1.0f + 0.5f * 0.0f), "a crossfade is halfway after half its time");
			instance.update(0.6f, events);
			check(instance.node_count() == 1, "a finished crossfade drops the old clip");

			// a crossfade started during another nests, then collapses; the tree stays bounded
			instance.play({ "Idle", {} });
			u32 largest = 0;
			for (int i = 0; i < 100; i++)
			{
				instance.crossfade({ i % 2 == 0 ? "Run" : "Idle", {} }, 0.2f);
				instance.update(0.01f, events);
				largest = std::max(largest, instance.node_count());
			}
			check(largest <= ANIMATOR_MAX_TREE_NODES + 2, "crossfades started in a row keep the tree bounded");
			instance.update(1.0f, events);
			check(instance.node_count() == 1, "and collapse to one clip when they have faded");

			// stop fades to the rest pose
			instance.play({ "Idle", {} });
			instance.stop(0.5f);
			instance.update(0.25f, events);
			check(near_equal(hip_x(instance), 0.5f), "stop fades toward the rest pose");
			instance.update(1.0f, events);
			check(!instance.playing() && near_equal(hip_x(instance), 0.0f), "and ends on it");

			// an unknown name is logged and dropped, not held forever
			instance.play({ "Nope", {} });
			check(instance.pending_commands() == 0, "an unknown clip name does not wait");
		}

		void layers(AnimCheck& check)
		{
			Fixture fixture;
			AnimatorInstance instance(fixture.Sources, fixture.config());
			std::vector<ClipEvent> events;
			instance.play({ "Idle", {} });

			LayerOptions options;
			options.Mask = "hand";
			options.Loop = true;
			const LayerId upper = instance.add_layer("upper", { "Wave", {} }, options);
			instance.update(0.1f, events);
			check(instance.layer_alive(upper) && near_equal(instance.pose().T[1].x, 5.0f) && near_equal(hip_x(instance), 1.0f), "a masked layer reaches only its joint");

			instance.set_layer_weight(upper, 0.5f);
			instance.update(0.1f, events);
			check(near_equal(instance.pose().T[1].x, 3.0f), "the layer weight blends it in: halfway from 1 to 5");

			instance.layer_stop(upper, 0.2f);
			instance.update(0.1f, events);
			check(instance.layer_alive(upper), "a stopping layer is still there while it fades");
			instance.update(0.2f, events);
			check(!instance.layer_alive(upper) && near_equal(instance.pose().T[1].x, 1.0f), "and gone when it has faded");
			instance.set_layer_weight(upper, 1.0f);
			check(near_equal(instance.layer_weight(upper), 0.0f), "a layer that ended ignores writes");

			// an additive layer adds the change of the clip from its first frame
			AnimationClipData lift;
			lift.Name = "Lift";
			lift.Duration = 1.0f;
			lift.Channels.push_back(x_channel(1, { 0.0f, 1.0f }, { 0.0f, 4.0f }));
			fixture.Clips[make_ref(9).Guid] = lift;
			AnimatorInstance additive(fixture.Sources, [&] { Animator a = fixture.config(); a.Clips.push_back(make_ref(9)); return a; }());
			additive.play({ "Idle", {} });
			LayerOptions addOptions;
			addOptions.Additive = true;
			addOptions.Weight = 0.5f;
			additive.add_layer("lift", { "Lift", {} }, addOptions);
			additive.update(0.5f, events);
			check(near_equal(additive.pose().T[0].x, 1.0f + 0.5f * 2.0f), "an additive layer adds half of the change at half weight");

			// a layer asked for before its clip loaded appears when it loads
			AnimatorInstance waiting(fixture.Sources, fixture.config());
			waiting.play({ "Idle", {} });
			fixture.LateLoaded = false;
			const LayerId pending = waiting.add_layer("late", { "Late", {} }, {});
			check(waiting.layer_alive(pending), "a layer waiting for its clip counts as alive");
		}

		void extraction(AnimCheck& check)
		{
			Fixture fixture;
			AnimationModule module;
			module.set_sources(fixture.Sources);
			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity hero = fixture.make_character(*scene);
			scene->registry().get<Transform>(hero).Position = glm::vec3(0.0f, 0.0f, 10.0f);
			scene->registry().get<Animator>(hero).BoundsPadding = 2.5f;
			const entt::entity body = scene->create("body", hero);
			scene->registry().get<Transform>(body).Position = glm::vec3(5.0f, 0.0f, 0.0f); // a skinned mesh node ignores its own transform
			scene->registry().emplace<MeshRenderer>(body, MeshRenderer{ make_ref(40), {} });
			scene->registry().emplace<SkinBinding>(body, SkinBinding{ fixture.SkeletonRef });
			const entt::entity prop = scene->create("rigid", hero);
			scene->registry().get<Transform>(prop).Position = glm::vec3(1.0f, 0.0f, 0.0f);
			scene->registry().emplace<MeshRenderer>(prop, MeshRenderer{ make_ref(41), {} });
			scene->update_world_transforms();

			PaletteSource palettes = [&module](entt::entity animator) -> std::span<const glm::vec4>
			{
				const AnimatorInstance* instance = module.instance_for(animator);
				if (instance == nullptr || instance->palette().empty())
				{
					return {};
				}
				return { reinterpret_cast<const glm::vec4*>(instance->palette().data()), instance->palette().size() * 3 };
			};
			auto world_of = [&](const SceneDelta& delta, entt::entity entity) -> std::optional<RenderInstance>
			{
				const EntityGuid guid = scene->registry().get<Id>(entity).Guid;
				for (const auto& [id, instance] : delta.InstanceUpserts)
				{
					if (id == guid)
					{
						return instance;
					}
				}
				return std::nullopt;
			};
			auto palette_of = [&](const SceneDelta& delta, entt::entity entity) -> const std::vector<glm::vec4>*
			{
				const EntityGuid guid = scene->registry().get<Id>(entity).Guid;
				for (const auto& [id, rows] : delta.PaletteUpdates)
				{
					if (id == guid)
					{
						return &rows;
					}
				}
				return nullptr;
			};

			// editing: no animator instance, so no palette; the mesh stands where the animator does
			SceneExtractor extractor;
			SceneDelta first = extractor.extract(scene, 1.0f, palettes);
			const std::optional<RenderInstance> editBody = world_of(first, body);
			check(editBody && near_equal(editBody->World[3].z, 10.0f) && near_equal(editBody->World[3].x, 0.0f), "a skinned mesh stands where its animator does, not at its own node");
			check(editBody && near_equal(editBody->BoundsPadding, 2.5f), "and its culling bounds carry the animator padding");
			check(palette_of(first, body) == nullptr, "an animator that is not running sends no palette");
			const std::optional<RenderInstance> rigid = world_of(first, prop);
			check(rigid && near_equal(rigid->World[3].x, 1.0f) && near_equal(rigid->BoundsPadding, 0.0f), "a rigid mesh is extracted as before");
			check(palette_of(first, prop) == nullptr, "and has no palette");

			// playing: the palette arrives, with no change of the instance
			module.tick(scene, 0.1f, true);
			SceneDelta second = extractor.extract(scene, 1.0f, palettes);
			const std::vector<glm::vec4>* sent = palette_of(second, body);
			check(sent != nullptr && sent->size() == 6 && second.InstanceUpserts.empty(), "the palette is sent alone: the instance did not change");

			// the same pose again: nothing is sent
			SceneDelta third = extractor.extract(scene, 1.0f, palettes);
			check(third.empty(), "an unchanged pose and instance send nothing");

			// a pose that changed with the world matrix unchanged
			module.instance_for(hero)->play({ "Run", {} });
			module.tick(scene, 0.5f, true);
			SceneDelta fourth = extractor.extract(scene, 1.0f, palettes);
			check(palette_of(fourth, body) != nullptr && fourth.InstanceUpserts.empty(), "a changed pose sends a palette and no instance");

			// the animator moves: the instance follows it
			scene->registry().get<Transform>(hero).Position = glm::vec3(0.0f, 0.0f, 20.0f);
			scene->update_world_transforms();
			SceneDelta fifth = extractor.extract(scene, 1.0f, palettes);
			const std::optional<RenderInstance> moved = world_of(fifth, body);
			check(moved && near_equal(moved->World[3].z, 20.0f), "the skinned instance follows the animator");

			// the animator is disabled: the palette ends (an empty one makes the instance rigid again)
			scene->registry().get<Animator>(hero).Enabled = false;
			module.tick(scene, 0.1f, true);
			SceneDelta sixth = extractor.extract(scene, 1.0f, palettes);
			const std::vector<glm::vec4>* cleared = palette_of(sixth, body);
			check(cleared != nullptr && cleared->empty(), "a stopped animator clears the palette");
		}

		void sockets(AnimCheck& check)
		{
			Fixture fixture;
			AnimationModule module;
			module.set_sources(fixture.Sources);
			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity hero = fixture.make_character(*scene);
			scene->registry().get<Transform>(hero).Position = glm::vec3(0.0f, 0.0f, 10.0f);
			const entt::entity prop = scene->create("prop", hero);
			scene->registry().emplace<AnimationSocket>(prop, AnimationSocket{ "hand", glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });

			module.tick(scene, 0.1f, true);
			module.instance_for(hero)->play({ "Run", {} });
			module.tick(scene, 0.5f, true);

			// the hip is at x = 5, the hand 1 further along (its rest pose), the offset lifts it by 1; the hero stands at z = 10
			const glm::vec3 world = glm::vec3(scene->registry().get<WorldTransform>(prop).Matrix[3]);
			check(near_equal(world.x, 6.0f) && near_equal(world.y, 1.0f) && near_equal(world.z, 10.0f), "a socket follows its joint, with its offset, in the world");
			const glm::vec3 joint = glm::vec3(module.joint_world(hero, "hand")[3]);
			check(near_equal(joint.x, 6.0f) && near_equal(joint.z, 10.0f), "joint_world is the joint in the world");
			check(module.joint_world(hero, "nope") == glm::mat4(1.0f), "joint_world of an unknown joint is the identity");

			// a socket for a joint the skeleton lacks is left where it is
			scene->registry().get<AnimationSocket>(prop).Joint = "tail";
			scene->registry().get<Transform>(prop).Position = glm::vec3(3.0f, 3.0f, 3.0f);
			module.tick(scene, 0.1f, true);
			check(near_equal(scene->registry().get<Transform>(prop).Position.x, 3.0f), "a socket with an unknown joint is not written");
		}
	}

	void run_module_checks(AnimCheck& check)
	{
		prefab_and_components(check);
		module_playback(check);
		previews(check);
		play_tree(check);
		layers(check);
		sockets(check);
		extraction(check);
	}
}
