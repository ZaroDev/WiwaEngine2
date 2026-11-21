#include <editor_common.h>
#include "demo_smoke.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <animation/animation_module.h>
#include <assets/asset_manager.h>
#include <audio/audio_module.h>
#include <audio/null_backend.h>
#include <core/engine.h>
#include <input/input_module.h>
#include <ai/ai_module.h>
#include <nav/nav_module.h>
#include <scene/particle_runtime.h>
#include <scene/reflection/component_registry.h>
#include <scripting/script_module.h>
#include <scripting/script_names.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_module.h>
#include <renderer/view/view_types.h>
#include <video/video_module.h>

namespace wiwa
{
	namespace
	{
		const char* const DEMO_SCENE = "assets/scenes/demo.wscene";
		const std::filesystem::path HOT_SCRIPT = "assets/scripts/zz_hot_smoke.lua";

		entt::entity find_named(Scene& scene, const std::string& name)
		{
			for (const entt::entity entity : scene.registry().view<Name>())
			{
				if (scene.registry().get<Name>(entity).Value == name)
				{
					return entity;
				}
			}
			return entt::null;
		}

		void write_hot_script(int value)
		{
			std::ofstream out(HOT_SCRIPT, std::ios::binary | std::ios::trunc);
			out << "return Script:extend{ on_update = function(self, dt) self.transform.position.x = " << value << " end }\n";
		}

		void remove_hot_script()
		{
			std::error_code error;
			std::filesystem::remove(HOT_SCRIPT, error);
			std::filesystem::remove(HOT_SCRIPT.string() + ".meta", error);
		}

		float position_x(Scene& scene, const EntityGuid& guid)
		{
			const entt::entity entity = scene.find(guid);
			return entity == entt::null ? -1.0f : scene.registry().get<Transform>(entity).Position.x;
		}
	}

	DemoSmoke::DemoSmoke(SceneEditor& editor, PlayMode& play) : m_Editor(editor), m_Play(play) {}

	DemoSmoke::~DemoSmoke()
	{
		restore_input(); // also when the run ended early
		remove_hot_script(); // also when the run ended early
	}

	// Waits are in seconds as well as frames: with vsync off a frame is a fraction of a millisecond, and the file watcher and the import need real time.
	// The animated warrior of the scene (an Animator the prefab expander made, the demo script that crossfades Survey and Run, a socket on its head).
	void DemoSmoke::sample_fox(Scene& play)
	{
		AnimationModule* animation = Engine::get().get_module<AnimationModule>();
		const entt::entity fox = find_named(play, "Warrior (animated)");
		const entt::entity socket = find_named(play, "Warrior head socket");
		if (animation == nullptr || fox == entt::null || socket == entt::null)
		{
			return;
		}

		if (m_RunFrames == 0)
		{
			m_Fox.EventsBefore = animation->events_total();
		}
		m_Fox.HasAnimator = m_Fox.HasAnimator || play.registry().all_of<Animator>(fox);
		m_Fox.EventsSeen = animation->events_total() - m_Fox.EventsBefore;

		// the decals of the demo: those the scene places ("Decal ..."), and the footprints the script of the warrior spawns on its footsteps
		u32 placed = 0;
		u32 footprints = 0;
		for (const auto [entity, decal, name] : play.registry().view<Decal, Name>().each())
		{
			(name.Value.rfind("Footprint", 0) == 0 ? footprints : placed)++;
		}
		// the lamp pair: a prefab that holds two lamp prefabs, the scene brightens one head (an override two levels deep), the pair prefab makes the other blue
		u32 bright = 0;
		u32 blue = 0;
		for (const auto [entity, light, name] : play.registry().view<Light, Name>().each())
		{
			if (name.Value == "Head" && play.registry().all_of<PrefabChild>(entity))
			{
				bright += std::abs(light.Intensity - 40.0f) < 0.01f ? 1 : 0;
				blue += light.Color.z > 0.9f && light.Color.x < 0.5f ? 1 : 0;
			}
		}
		m_Fox.NestedBright = std::max(m_Fox.NestedBright, bright);
		m_Fox.NestedBlue = std::max(m_Fox.NestedBlue, blue);
		m_Fox.PlacedDecals = std::max(m_Fox.PlacedDecals, placed);
		m_Fox.MostFootprints = std::max(m_Fox.MostFootprints, footprints);

		// the particle effects of the demo: the systems the scene places (playing), the dust of the warrior's footsteps and the sparks the script of the station spawns
		u32 placedEffects = 0;
		u32 dust = 0;
		u32 sparks = 0;
		for (const auto [entity, system, name] : play.registry().view<ParticleSystem, Name>().each())
		{
			if (name.Value == "Footstep dust")
			{
				dust++;
			}
			else if (name.Value == "Impact sparks")
			{
				sparks++;
			}
			else if (const ParticleRuntime* runtime = play.registry().try_get<ParticleRuntime>(entity); runtime != nullptr && runtime->Playing)
			{
				placedEffects++;
			}
		}
		m_Fox.PlayingEffects = std::max(m_Fox.PlayingEffects, placedEffects);
		m_Fox.MostDust = std::max(m_Fox.MostDust, dust);
		m_Fox.MostSparks = std::max(m_Fox.MostSparks, sparks);

		m_Fox.WarriorMeshes = 0;
		m_Fox.WarriorMeshesReceiving = 0;
		for (const auto [entity, renderer, hierarchy] : play.registry().view<MeshRenderer, Hierarchy>().each())
		{
			for (entt::entity up = hierarchy.Parent; up != entt::null; up = play.registry().get<Hierarchy>(up).Parent)
			{
				if (up == fox)
				{
					m_Fox.WarriorMeshes++;
					m_Fox.WarriorMeshesReceiving += renderer.ReceivesDecals ? 1 : 0;
					break;
				}
			}
		}

		const glm::vec3 position = play.registry().get<Transform>(socket).Position;
		m_Fox.SocketLow = glm::min(m_Fox.SocketLow, position);
		m_Fox.SocketHigh = glm::max(m_Fox.SocketHigh, position);

		if (const AnimatorInstance* instance = animation->instance_for(fox))
		{
			m_Fox.Crossfaded = m_Fox.Crossfaded || instance->node_count() > 1;
			if (instance->graph_mode())
			{
				m_Fox.GraphSeen = true;
				m_Fox.BaseStates.insert(instance->state_name(0));
				m_Fox.UpperStates.insert(instance->state_name(1));
			}
			for (const JointMatrix& joint : instance->palette())
			{
				const glm::vec4 identity[3] = { glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0) };
				for (int row = 0; row < 3; row++)
				{
					m_Fox.PaletteMoved = m_Fox.PaletteMoved || glm::length(joint.Row[row] - identity[row]) > 0.05f;
				}
			}
		}
	}

	std::string DemoSmoke::check_fox() const
	{
		if (!m_Fox.HasAnimator)
		{
			return "the warrior has no Animator (the prefab expander adds one to a model with animations)";
		}
		if (glm::length(m_Fox.SocketHigh - m_Fox.SocketLow) < 0.001f)
		{
			return "the head socket of the warrior did not move";
		}
		if (!m_Fox.PaletteMoved)
		{
			return "the warrior never left its rest pose";
		}
		if (!m_Fox.GraphSeen)
		{
			return "the warrior never played its animation graph";
		}
		if (!m_Fox.BaseStates.contains("Idle") || !m_Fox.BaseStates.contains("Move"))
		{
			return "the warrior graph did not go between Idle and Move as the script changed its speed";
		}
		if (!m_Fox.UpperStates.contains("Attack"))
		{
			return "the upper body layer of the warrior graph never played Attack when the script fired the trigger";
		}
		if (m_Fox.EventsSeen == 0)
		{
			return "no footstep of the warrior was fired";
		}
		if (m_Fox.NestedBright == 0 || m_Fox.NestedBlue == 0)
		{
			return "the lamp pair (a prefab that holds two prefabs) did not get its overrides: " + std::to_string(m_Fox.NestedBright) + " bright head(s) from the scene, " + std::to_string(m_Fox.NestedBlue) + " blue head(s) from the pair prefab";
		}
		if (m_Fox.PlacedDecals < 6)
		{
			return "the demo scene's decals are missing (a rune circle, four bullet holes and a scorch mark; found " + std::to_string(m_Fox.PlacedDecals) + ")";
		}
		if (m_Fox.MostFootprints == 0)
		{
			return "the footsteps of the warrior spawned no footprint decals (decal_footprints.lua, Decal.spawn)";
		}
		if (m_Fox.WarriorMeshes == 0 || m_Fox.WarriorMeshesReceiving != 0)
		{
			return "decal_footprints.lua did not turn off ReceivesDecals on the meshes of the warrior (" + std::to_string(m_Fox.WarriorMeshesReceiving) + " of " + std::to_string(m_Fox.WarriorMeshes) + " still receive)";
		}
		if (m_Fox.PlayingEffects < 3)
		{
			return "the demo scene's particle effects are not playing (a campfire, a spark fountain and the rune motes; found " + std::to_string(m_Fox.PlayingEffects) + ")";
		}
		if (m_Fox.MostDust == 0)
		{
			return "the footsteps of the warrior spawned no dust (vfx_footsteps.lua, Vfx.spawn)";
		}
		if (m_Fox.MostSparks == 0)
		{
			return "vfx_demo.lua spawned no impact sparks (Vfx.spawn)";
		}
		return {};
	}

	void DemoSmoke::sample_audio(Scene& play)
	{
		AudioModule* audio = Engine::get().get_module<AudioModule>();
		const entt::entity entity = find_named(play, "Audio demo");
		if (audio == nullptr || entity == entt::null)
		{
			return;
		}

		m_Audio.HasEmitter = m_Audio.HasEmitter || play.registry().all_of<AudioEmitter>(entity);
		m_Audio.Registered = m_Audio.Registered || audio->emitter_of(entity).valid();
		m_Audio.NullBackend = audio->active_kind() == AudioBackendKind::Null;
		m_Audio.WwiseWithBanks = (audio->active_kind() == AudioBackendKind::Wwise && !audio->banks().empty()) || audio->active_kind() == AudioBackendKind::MiniAudio; // a backend that plays: something must
		m_Audio.Playing = m_Audio.Playing || audio->backend().playing_count() > 0;

		if (m_Audio.NullBackend && (!m_Audio.LoopPosted || !m_Audio.ScriptPosted))
		{
			for (const std::string& call : static_cast<NullAudioBackend&>(audio->backend()).calls())
			{
				m_Audio.LoopPosted = m_Audio.LoopPosted || call.rfind("post:Amb_Hall_loop@", 0) == 0;
				m_Audio.ScriptPosted = m_Audio.ScriptPosted || call.rfind("post:Beacon_Blip@", 0) == 0;
			}
		}
	}

	std::string DemoSmoke::check_audio() const
	{
		if (!m_Audio.HasEmitter)
		{
			return "the Audio demo entity has no AudioEmitter";
		}
		if (!m_Audio.Registered)
		{
			return "the audio module never gave the Audio demo emitter a voice in the backend";
		}
		if (m_Audio.NullBackend && !m_Audio.LoopPosted)
		{
			return "the PlayOnStart event (the ambience) of the Audio demo emitter never reached the audio backend";
		}
		if (m_Audio.NullBackend && !m_Audio.ScriptPosted)
		{
			return "the Audio.post call (the beacon blip) of the audio demo script never reached the audio backend";
		}
		if (m_Audio.WwiseWithBanks && !m_Audio.Playing)
		{
			return "the audio backend has sounds to play but nothing played in the Audio demo (Settings, Audio: Wwise needs its banks, MiniAudio the clips in assets/audio/clips)";
		}
		return {};
	}

	void DemoSmoke::sample_ai(Scene& play)
	{
		AIModule* ai = Engine::get().get_module<AIModule>();
		const entt::entity guard = find_named(play, "Guard");
		const entt::entity thief = find_named(play, "Thief");
		if (ai == nullptr || guard == entt::null || thief == entt::null)
		{
			return;
		}
		const BtInstance* instance = ai->instance_of(guard);
		if (instance == nullptr)
		{
			return;
		}
		m_Ai.HasController = true;
		const Blackboard& board = instance->blackboard();
		const std::optional<BtValue> sees = board.get("can_see_target");
		const std::optional<BtValue> seen = board.get("seen_target");
		if (sees && seen && std::get<bool>(*sees) && play.registry().all_of<Id>(thief) && std::get<EntityGuid>(*seen) == play.registry().get<Id>(thief).Guid)
		{
			m_Ai.Saw = true;
		}
		if (const std::optional<BtValue> heard = board.get("heard_kind"); heard && std::get<std::string>(*heard) == "crash")
		{
			m_Ai.Heard = true;
		}
		if (play.registry().all_of<WorldTransform>(guard) && play.registry().all_of<WorldTransform>(thief))
		{
			m_Ai.Closest = std::min(m_Ai.Closest, glm::distance(glm::vec3(play.registry().get<WorldTransform>(guard).Matrix[3]), glm::vec3(play.registry().get<WorldTransform>(thief).Matrix[3])));
		}
	}

	bool DemoSmoke::ai_done() const
	{
		return m_Ai.HasController && m_Ai.Saw && m_Ai.Heard && m_Ai.Closest < 3.0f;
	}

	std::string DemoSmoke::check_ai() const
	{
		if (!m_Ai.HasController)
		{
			return "the AIModule never made a behaviour tree instance for the Guard of the navigation station (its guard.wbt, AIController and the imported tree)";
		}
		if (!m_Ai.Saw)
		{
			return "the Guard never saw the Thief (a Perceiver sight pass should have put it in seen_target with can_see_target true)";
		}
		if (!m_Ai.Heard)
		{
			return "the Guard never heard the noise maker (heard_kind \"crash\": AI.emit_noise in noise_maker.lua, the Perceiver hearing)";
		}
		if (!(m_Ai.Closest < 3.0f))
		{
			return "the Guard saw the Thief but never came within 3 m of it (closest " + std::to_string(m_Ai.Closest) + " m): the chase branch, MoveTo or the navmesh";
		}
		return {};
	}

	void DemoSmoke::sample_nav(Scene& play)
	{
		NavModule* nav = Engine::get().get_module<NavModule>();
		if (nav == nullptr)
		{
			WI_LOG(Warning, "DemoSmoke", "SKIPPED: sample_nav: no NavModule is registered");
			return;
		}
		m_Nav.HasMesh = m_Nav.HasMesh || nav->has_navmesh();

		u32 found = 0;
		for (int i = 0; i < 3; i++)
		{
			const entt::entity agent = find_named(play, "Nav agent " + std::to_string(i + 1));
			if (agent == entt::null || !play.registry().all_of<NavAgent, WorldTransform>(agent))
			{
				continue;
			}
			found++;
			const glm::vec3 goal(-34.0f + 4.0f * static_cast<float>(i), 0.0f, -8.0f);
			const glm::vec3 position(play.registry().get<WorldTransform>(agent).Matrix[3]);
			m_Nav.Closest[i] = std::min(m_Nav.Closest[i], glm::distance(position, goal));
		}
		m_Nav.Agents = std::max(m_Nav.Agents, found);
	}

	bool DemoSmoke::nav_arrived() const
	{
		return m_Nav.Agents == 3 && m_Nav.Closest[0] < 1.2f && m_Nav.Closest[1] < 1.2f && m_Nav.Closest[2] < 1.2f;
	}

	std::string DemoSmoke::check_nav() const
	{
		if (m_Nav.Agents != 3)
		{
			return "the navigation station's three agents (Nav agent 1..3 with a NavAgent) are not in the scene";
		}
		if (!m_Nav.HasMesh)
		{
			return "the navigation module never had a navmesh for the demo scene (bake it: wiwa_editor --scene assets/scenes/demo.wscene --nav-bake <the volume's .wnav>, see scripts/gen_demo_scene.py)";
		}
		if (const NavModule* nav = Engine::get().get_module<NavModule>(); nav == nullptr || nav->settings().AreaName[4] != "Swamp" || !(nav->settings().AreaCost[4] > 1.0f) || nav->settings().AreaName[2] != "Mud")
		{
			return "the project's navigation areas (editor/nav.wsettings) are not Mud (2) and the Swamp (4, a cost above 1) the demo's navigation station uses";
		}
		for (int i = 0; i < 3; i++)
		{
			if (!(m_Nav.Closest[i] < 1.2f))
			{
				return "Nav agent " + std::to_string(i + 1) + " never got within 1.2 m of its goal on the other side of the wall (closest " + std::to_string(m_Nav.Closest[i]) + " m): nav_patrol.lua, the NavAgent or the baked navmesh";
			}
		}
		return {};
	}

	bool DemoSmoke::wait(u32 frames, const char* what, std::string& failure)
	{
		if (m_PhaseFrames == 0)
		{
			m_PhaseStart = std::chrono::steady_clock::now();
		}
		const bool tooLong = std::chrono::steady_clock::now() - m_PhaseStart > std::chrono::seconds(30);
		if (++m_PhaseFrames > frames && tooLong)
		{
			failure = std::string("timed out waiting for ") + what;
			m_Phase = Phase::Done;
			return false;
		}
		return true;
	}

	// The memory work of the demo: the ground, the backdrop and the PBR spheres are marked Static (a crate is not), and the sphere mesh the PBR grid draws was
	// imported with levels of detail (the model importer bakes them), each coarser than the one before.
	std::string DemoSmoke::check_static_and_lods(Scene& scene, AssetManager* assets)
	{
		const auto renderer_of = [&](const char* name) -> const MeshRenderer*
		{
			const entt::entity entity = find_named(scene, name);
			return entity == entt::null ? nullptr : scene.registry().try_get<MeshRenderer>(entity);
		};
		const MeshRenderer* ground = renderer_of("Ground");
		const MeshRenderer* backdrop = renderer_of("Backdrop");
		const MeshRenderer* sphere = renderer_of("PBR r0 m0");
		const MeshRenderer* crate = renderer_of("Crate 1");
		if (ground == nullptr || backdrop == nullptr || sphere == nullptr || crate == nullptr)
		{
			return "the demo scene lacks the Ground, Backdrop, PBR sphere or Crate 1 the memory check reads";
		}
		if (!ground->Static || !backdrop->Static || !sphere->Static || crate->Static)
		{
			return "the ground, the backdrop and the PBR spheres have to be Static and the crates not (Ground " + std::to_string(ground->Static) + ", Backdrop "
				+ std::to_string(backdrop->Static) + ", sphere " + std::to_string(sphere->Static) + ", crate " + std::to_string(crate->Static) + ")";
		}

		Handle<MeshData> mesh = assets->load<MeshData>(sphere->Mesh);
		assets->wait_idle();
		const MeshData* data = mesh.get();
		if (data == nullptr || data->Primitives.empty())
		{
			return "the sphere mesh of the PBR grid did not load";
		}
		const MeshPrimitive& primitive = data->Primitives[0];
		bool coarser = primitive.LodCount > 1;
		for (u32 level = 1; level < primitive.LodCount; level++)
		{
			coarser = coarser && primitive.Lods[level].IndexCount < primitive.Lods[level - 1].IndexCount && primitive.Lods[level].Error > primitive.Lods[level - 1].Error;
		}
		if (!coarser)
		{
			return "the sphere mesh has " + std::to_string(primitive.LodCount) + " level(s) of detail: the model importer should have baked coarser ones";
		}
		WI_INFO("Editor", "Demo smoke: memory ok (static ground, sphere with {0} levels: {1} down to {2} triangles)", primitive.LodCount, primitive.Lods[0].IndexCount / 3,
			primitive.Lods[primitive.LodCount - 1].IndexCount / 3);
		return {};
	}

	bool DemoSmoke::update(std::string& failure)
	{
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		ScriptingModule* scripting = Engine::get().get_module<ScriptingModule>();

		if (!m_Started)
		{
			std::error_code error;
			m_LogStart = std::filesystem::exists("wiwa.log", error) ? std::filesystem::file_size("wiwa.log", error) : 0;
			m_Started = true;
		}

		// the joints and character demos are watched from the start of the play to the end of the Input phase
		if (m_Phase >= Phase::Run && m_Phase <= Phase::Physics && m_Play.play_scene() != nullptr && !sample_physics(*m_Play.play_scene(), failure))
		{
			m_Phase = Phase::Done;
			return true;
		}

		switch (m_Phase)
		{
		case Phase::Open:
		{
			if (m_PhaseFrames == 0)
			{
				m_Editor.open(DEMO_SCENE);
			}
			if (!wait(1800, "the demo scene to open", failure))
			{
				return true;
			}
			if (!m_Editor.is_opening() && m_Editor.get_scene() != nullptr && find_named(*m_Editor.get_scene(), "Blue ring") != entt::null)
			{
				m_Phase = Phase::Play;
				m_PhaseFrames = 0;
			}
			return false;
		}

		case Phase::Play:
		{
			if (!wait(600, "play to start", failure))
			{
				return true;
			}
			Scene& edit = *m_Editor.get_scene();
			if (const std::string memory = check_static_and_lods(edit, assets); !memory.empty())
			{
				failure = memory;
				return true;
			}
			m_RingBefore = edit.registry().get<Transform>(find_named(edit, "Blue ring")).Rotation.x;
			m_Play.play(); // refused while a prefab expansion or a load is still going: tried again next frame
			if (m_Play.state() == PlayState::Playing)
			{
				m_Phase = Phase::Run;
				m_PhaseFrames = 0;
				m_RunFrames = 0;
			}
			return false;
		}

		case Phase::Run:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			Scene& play = *m_Play.play_scene();
			const entt::entity ring = find_named(play, "Blue ring");
			const entt::entity ball = find_named(play, "Neon cyan");
			const entt::entity camera = find_named(play, "Camera");
			const entt::entity spot = find_named(play, "Podium spot");
			if (ring == entt::null || ball == entt::null || camera == entt::null || spot == entt::null)
			{
				failure = "the demo entities are missing in the running scene";
				return true;
			}

			m_RingAfter = play.registry().get<Transform>(ring).Rotation.x;
			const float y = play.registry().get<Transform>(ball).Position.y;
			const float x = play.registry().get<Transform>(camera).Position.x;
			const float intensity = play.registry().get<Light>(spot).Intensity;
			m_BallLow = std::min(m_BallLow, y);
			m_BallHigh = std::max(m_BallHigh, y);
			m_CameraLow = std::min(m_CameraLow, x);
			m_CameraHigh = std::max(m_CameraHigh, x);
			m_LightLow = std::min(m_LightLow, intensity);
			m_LightHigh = std::max(m_LightHigh, intensity);

			sample_fox(play);
			sample_audio(play);
			sample_nav(play);
			sample_ai(play);

			// 150 frames, and the agents get up to 25 seconds (frames are far shorter than the game's seconds here) to cross the arena: the navmesh loads a few
			// frames after the play starts
			if (m_RunFrames == 0)
			{
				m_NavDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
			}
			if (++m_RunFrames < 150 || ((!nav_arrived() || !ai_done()) && std::chrono::steady_clock::now() < m_NavDeadline))
			{
				return false;
			}

			// first: the navigation demo does not depend on the other stations
			if (const std::string navigation = check_nav(); !navigation.empty())
			{
				failure = navigation;
				return true;
			}
			if (const std::string intelligence = check_ai(); !intelligence.empty())
			{
				failure = intelligence;
				return true;
			}
			WI_INFO("Editor", "Demo smoke: behaviour trees ok (the guard saw the thief and heard the noise, closest {0:.2f} m)", m_Ai.Closest);
			WI_INFO("Editor", "Demo smoke: navigation ok (the agents came within {0:.2f}, {1:.2f} and {2:.2f} m of their goals)", m_Nav.Closest[0], m_Nav.Closest[1], m_Nav.Closest[2]);

			if (const std::string fox = check_fox(); !fox.empty())
			{
				failure = fox;
				return true;
			}

			if (const std::string audio = check_audio(); !audio.empty())
			{
				failure = audio;
				return true;
			}

			if (!scripting->running())
			{
				failure = "the scripts are not running";
				return true;
			}
			if (m_RingAfter == m_RingBefore)
			{
				failure = "spinner did not turn the ring";
				return true;
			}
			if (m_BallHigh - m_BallLow < 0.01f)
			{
				failure = "bobber did not move the neon ball";
				return true;
			}
			if (m_CameraHigh - m_CameraLow < 0.001f)
			{
				failure = "camera_sway did not move the camera";
				return true;
			}
			if (m_LightHigh - m_LightLow < 0.5f)
			{
				failure = "light_pulse did not change the spot light";
				return true;
			}
			m_Phase = Phase::Ui;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::Physics:
		{
			// what sample_physics watched since the play started (the motors turn around every 2.5 to 3 seconds, so the run has seen whole swings)
			const PhysicsWatch& watch = m_Watch;
			if (!(watch.DoorSwing > 0.5f && watch.DoorSwing < 0.82f))
			{
				failure = "the door did not swing within its limits (largest |rotation.y| " + std::to_string(watch.DoorSwing) + ", expected 0.5 to 0.82)";
				return true;
			}
			if (!(watch.CartHigh - watch.CartLow > 2.0f && watch.CartLow > -14.4f && watch.CartHigh < -9.6f))
			{
				failure = "the cart did not slide along its rail within its limits (x " + std::to_string(watch.CartLow) + " to " + std::to_string(watch.CartHigh) + ")";
				return true;
			}
			if (watch.BobPath < 1.5f)
			{
				failure = "the spring pendulum did not swing (it travelled " + std::to_string(watch.BobPath) + " m)";
				return true;
			}
			if (watch.BridgeStretch > 1.25f || watch.BridgeLow < 1.0f)
			{
				failure = "the bridge came apart (longest link distance " + std::to_string(watch.BridgeStretch) + ", lowest plank " + std::to_string(watch.BridgeLow) + ")";
				return true;
			}
			if (watch.WeakLow > 3.0f)
			{
				failure = "the hanging box never broke away (lowest y " + std::to_string(watch.WeakLow) + ")";
				return true;
			}
			if (!(watch.PatrolHighX - watch.PatrolLowX > 4.0f && watch.PatrolLowY > -0.3f && watch.PatrolHighY > 0.9f))
			{
				failure = "the patrolling character did not walk and jump on its lane (x " + std::to_string(watch.PatrolLowX) + " to " + std::to_string(watch.PatrolHighX) + ", y " + std::to_string(watch.PatrolLowY) + " to " + std::to_string(watch.PatrolHighY) + ")";
				return true;
			}
			if (!(watch.PlatformHigh - watch.PlatformLow > 2.0f))
			{
				failure = "the platform did not slide (x " + std::to_string(watch.PlatformLow) + " to " + std::to_string(watch.PlatformHigh) + ")";
				return true;
			}
			m_Phase = Phase::HotCreate;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::Ui:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				restore_ui();
				return true;
			}
			if (!wait(4000, "the HUD checks", failure))
			{
				restore_ui();
				return true;
			}

			switch (run_ui_step(failure))
			{
			case UiResult::Running:
				return false;
			case UiResult::Failed:
				restore_ui();
				m_Phase = Phase::Done;
				return true;
			case UiResult::Passed:
				restore_ui();
				m_Phase = Phase::Input;
				m_PhaseFrames = 0;
				return false;
			}
			return false;
		}

		case Phase::Input:
		{
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				restore_input();
				return true;
			}
			if (!wait(3000, "the input checks", failure))
			{
				restore_input();
				return true;
			}

			switch (run_input_step(failure))
			{
			case UiResult::Running:
				return false;
			case UiResult::Failed:
				restore_input();
				m_Phase = Phase::Done;
				return true;
			case UiResult::Passed:
				restore_input();
				m_Phase = Phase::Physics;
				m_PhaseFrames = 0;
				return false;
			}
			return false;
		}

		case Phase::HotCreate:
		{
			if (m_PhaseFrames == 0)
			{
				write_hot_script(111);
			}
			if (!wait(1800, "the new script to be imported", failure))
			{
				return true;
			}

			const std::optional<AssetRef> ref = assets->find_asset(HOT_SCRIPT.generic_string());
			if (!ref)
			{
				return false;
			}
			const ComponentInfo* info = ComponentRegistry::get().find(script_component_name(ref->Guid));
			if (info == nullptr)
			{
				return false;
			}

			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			Scene& play = *m_Play.play_scene();
			const entt::entity probe = play.create("hot probe");
			info->Add(play.registry(), probe);
			m_Probe = play.registry().get<Id>(probe).Guid;
			m_HotScript = *ref;
			m_Phase = Phase::HotFirst;
			m_PhaseFrames = 0;
			return false;
		}

		case Phase::HotFirst:
		{
			if (!wait(900, "the new script to run", failure))
			{
				return true;
			}
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			if (position_x(*m_Play.play_scene(), m_Probe) == 111.0f)
			{
				write_hot_script(222); // saved while the scene runs
				m_Phase = Phase::HotSecond;
				m_PhaseFrames = 0;
			}
			return false;
		}

		case Phase::HotSecond:
		{
			if (!wait(900, "the running script to pick up the new file", failure))
			{
				return true;
			}
			if (m_Play.play_scene() == nullptr)
			{
				failure = "play stopped by itself";
				return true;
			}
			if (position_x(*m_Play.play_scene(), m_Probe) == 222.0f)
			{
				failure = finish_checks();
				return true;
			}
			return false;
		}

		case Phase::Done:
			return true;
		}
		return true;
	}

	namespace
	{
		// The first quad of the list drawn in this colour, if there is one. The HUD of the demo gives each widget it looks for its own colour.
		std::optional<UiRect> find_quad(const UiDrawList& list, const glm::vec4& color)
		{
			const u32 packed = ui_pack_color(color);
			for (size_t quad = 0; quad * 4 + 3 < list.Vertices.size(); quad++)
			{
				if (list.Vertices[quad * 4].Color == packed)
				{
					return UiRect{ list.Vertices[quad * 4].Pos, list.Vertices[quad * 4 + 2].Pos };
				}
			}
			return std::nullopt;
		}

		// The colours the HUD panel (assets/ui/demo_hud.wuipanel) gives the widgets the smoke test looks for.
		const glm::vec4 BUTTON_COLOR{ 0.2f, 0.35f, 0.6f, 1.0f };      // ToggleLight, while the pointer is not on it
		const glm::vec4 SLIDER_FILL_COLOR{ 0.95f, 0.6f, 0.1f, 1.0f }; // LightIntensity
		const glm::vec4 BEACON_TRACK_COLOR{ 0.9f, 0.2f, 0.9f, 0.8f };  // BeaconBar, which follows the Beacon entity

		const glm::vec4 REBIND_COLOR{ 0.55f, 0.2f, 0.5f, 1.0f };       // RebindJump, while the pointer is not on it
		const glm::vec4 INTRO_BUTTON_COLOR{ 0.15f, 0.45f, 0.35f, 1.0f }; // PlayIntro, while the pointer is not on it
		const glm::vec4 SKIP_BUTTON_COLOR{ 0.5f, 0.25f, 0.15f, 1.0f };   // Intro/Skip, while the pointer is not on it
		const char* const INPUT_OVERRIDES_FILE = "input_overrides.json";

		const glm::vec2 HUD_CANVAS{ 1920.0f, 1080.0f };
	}

	void DemoSmoke::restore_ui()
	{
		if (!m_UiTouched)
		{
			return;
		}
		m_UiTouched = false;
		if (UiModule* ui = Engine::get().get_module<UiModule>())
		{
			ui->set_canvas_override(std::nullopt);
			ui->scripted_input().clear_pointer();
			ui->set_input_source(m_UiPreviousInput);
		}
	}

	// The HUD of the demo: it is drawn, the beacon bar follows its entity, the button and the slider reach the script (through scripted
	// input, the same path a mouse takes), and the script changes the light and the texts.
	DemoSmoke::UiResult DemoSmoke::run_ui_step(std::string& failure)
	{
		UiModule* ui = Engine::get().get_module<UiModule>();
		Scene& play = *m_Play.play_scene();
		const entt::entity hud = find_named(play, "Hud");
		const entt::entity light = find_named(play, "Demo light");
		const entt::entity beacon = find_named(play, "Beacon");
		if (ui == nullptr || hud == entt::null || light == entt::null || beacon == entt::null)
		{
			failure = "the demo scene has no HUD (an entity Hud with a UICanvas, a Demo light and a Beacon)";
			return UiResult::Failed;
		}
		const EntityGuid hudGuid = play.registry().get<Id>(hud).Guid;

		const auto intensity = [&]() { return play.registry().get<Light>(light).Intensity; };
		const auto draw = [&]()
		{
			play.update_world_transforms();
			UiDrawList list;
			ui->build_draw_list(play, HUD_CANVAS, list);
			return list;
		};
		const auto click_at = [&](const glm::vec2& point)
		{
			ui->scripted_input().move_to(point);
			ui->scripted_input().press(0);
			ui->scripted_input().release(0);
		};
		m_UiStepFrames++;

		switch (m_UiStep)
		{
		case UiStep::Setup:
		{
			m_UiPreviousInput = ui->input_source();
			ui->set_canvas_override(HUD_CANVAS);
			ui->set_input_source(&ui->scripted_input());
			ui->scripted_input().clear_pointer();
			m_UiTouched = true;
			m_UiStep = UiStep::WaitDrawn;
			m_UiStepFrames = 0;
			return UiResult::Running;
		}

		case UiStep::WaitDrawn:
		{
			UiPanelInstance* instance = ui->instance_for(play, hudGuid);
			const UiDrawList list = instance != nullptr && instance->ready() ? draw() : UiDrawList{};
			const std::optional<UiRect> button = find_quad(list, BUTTON_COLOR);
			const std::optional<UiRect> slider = find_quad(list, SLIDER_FILL_COLOR);
			const std::optional<UiRect> track = find_quad(list, BEACON_TRACK_COLOR);
			const bool text = std::any_of(list.Batches.begin(), list.Batches.end(), [](const UiBatch& batch) { return batch.Kind == UiQuadKind::Sdf; });

			// the script counts the score up once the panel has loaded and it has found its widgets
			const std::optional<UiId> score = instance != nullptr ? instance->find("Stats/Score") : std::nullopt;
			const bool counting = score && instance->data().find(*score)->Text.rfind("Score ", 0) == 0 && instance->data().find(*score)->Text != "Score 0";

			if (!button || !slider || !track || !text || !counting || intensity() < 0.5f)
			{
				if (m_UiStepFrames > 900)
				{
					failure = std::string("the HUD was not drawn: ") + (instance == nullptr ? "no panel instance" : !instance->ready() ? "the panel is not loaded" : list.empty() ? "an empty draw list" :
						!button ? "no button" : !slider ? "no slider" : !track ? "no beacon bar (is the Beacon on the screen?)" : !text ? "no text (is the font loaded?)" :
						!counting ? "the script did not count the score up (did demo_ui.lua start without an error?)" : "the light is off at the start");
					return UiResult::Failed;
				}
				return UiResult::Running;
			}

			m_ButtonCenter = (button->Min + button->Max) * 0.5f;
			const std::optional<UiId> sliderId = instance->find("Controls/LightIntensity");
			const UiWidget* sliderWidget = sliderId ? instance->data().find(*sliderId) : nullptr;
			if (sliderWidget == nullptr)
			{
				failure = "the HUD has no Controls/LightIntensity slider";
				return UiResult::Failed;
			}
			const float width = sliderWidget->OffsetMax.x - sliderWidget->OffsetMin.x;
			m_SliderPoint = glm::vec2(slider->Min.x + width * 0.5f, (slider->Min.y + slider->Max.y) * 0.5f);
			m_BeaconX = track->Min.x;
			m_LightLevel = intensity();

			play.registry().get<Transform>(beacon).Position.x += 3.0f;
			m_UiStep = UiStep::MoveBeacon;
			m_UiStepFrames = 0;
			return UiResult::Running;
		}

		case UiStep::MoveBeacon:
		{
			const std::optional<UiRect> track = find_quad(draw(), BEACON_TRACK_COLOR);
			if (!track)
			{
				failure = "the beacon bar vanished when its entity moved";
				return UiResult::Failed;
			}
			if (track->Min.x < m_BeaconX + 20.0f)
			{
				failure = "the beacon bar did not follow its entity to the right";
				return UiResult::Failed;
			}

			click_at(m_ButtonCenter);
			m_UiStep = UiStep::ClickOff;
			m_UiStepFrames = 0;
			return UiResult::Running;
		}

		case UiStep::ClickOff:
		{
			if (intensity() < 0.01f)
			{
				click_at(m_ButtonCenter);
				m_UiStep = UiStep::ClickOn;
				m_UiStepFrames = 0;
			}
			else if (m_UiStepFrames > 60)
			{
				failure = "clicking the button did not switch the light off";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case UiStep::ClickOn:
		{
			if (std::abs(intensity() - m_LightLevel) < 0.01f)
			{
				// press on the middle of the slider and hold: the value follows the pointer
				ui->scripted_input().move_to(m_SliderPoint);
				ui->scripted_input().press(0);
				m_UiStep = UiStep::Slide;
				m_UiStepFrames = 0;
			}
			else if (m_UiStepFrames > 60)
			{
				failure = "clicking the button again did not switch the light on";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case UiStep::Slide:
		{
			if (std::abs(intensity() - 10.0f) < 0.75f)
			{
				ui->scripted_input().release(0);
				m_UiStep = UiStep::Release;
				m_UiStepFrames = 0;
			}
			else if (m_UiStepFrames > 60)
			{
				failure = "dragging the slider to its middle did not set the light to half of its range (it is " + std::to_string(intensity()) + ")";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case UiStep::Release:
		{
			if (m_UiStepFrames > 3)
			{
				m_UiStep = UiStep::IntroClick;
				m_UiStepFrames = 0;
			}
			return UiResult::Running;
		}

		// The intro video: the real clip, decoded by the platform decoder on its worker, uploaded to the real renderer and drawn by the HUD.
		case UiStep::IntroClick:
		case UiStep::IntroPlaying:
		case UiStep::IntroSkipped:
		case UiStep::IntroReplay:
		case UiStep::IntroEnded:
		{
			VideoModule* video = Engine::get().get_module<VideoModule>();
			UiPanelInstance* instance = ui->instance_for(play, hudGuid);
			const std::optional<UiId> introId = instance != nullptr ? instance->find("Intro") : std::nullopt;
			const UiWidget* intro = introId ? instance->data().find(*introId) : nullptr;
			if (video == nullptr || intro == nullptr || intro->Kind != WidgetKind::Video)
			{
				failure = "the HUD has no Intro video widget (assets/ui/demo_hud.wuipanel, written by scripts/gen_demo_scene.py)";
				return UiResult::Failed;
			}
			const VideoKey key{ hudGuid, *introId };
			const VideoState state = video->state(key);
			const UiDrawList list = draw();
			const bool picture = std::any_of(list.Batches.begin(), list.Batches.end(), [](const UiBatch& batch)
				{
					return batch.Kind == UiQuadKind::Image && batch.ViewTexture >= FIRST_VIDEO_TEXTURE_ID;
				});
			if (state == VideoState::Failed)
			{
				failure = "the intro video failed: " + video->error(key);
				return UiResult::Failed;
			}
			const auto center = [](const UiRect& rect) { return (rect.Min + rect.Max) * 0.5f; };

			if (m_UiStep == UiStep::IntroClick)
			{
				const std::optional<UiRect> button = find_quad(list, INTRO_BUTTON_COLOR);
				if (!button)
				{
					failure = "the HUD shows no Play intro button";
					return UiResult::Failed;
				}
				if (intro->Visible || state != VideoState::Idle || picture)
				{
					failure = "the intro is showing or playing before anybody asked for it";
					return UiResult::Failed;
				}
				click_at(center(*button));
				m_UiStep = UiStep::IntroPlaying;
				m_UiStepFrames = 0;
				return UiResult::Running;
			}

			if (m_UiStep == UiStep::IntroPlaying || m_UiStep == UiStep::IntroReplay)
			{
				const bool replay = m_UiStep == UiStep::IntroReplay;
				if (state == VideoState::Playing && intro->Visible && picture && video->time(key) > 0.3)
				{
					if (!replay)
					{
						const std::optional<UiRect> skip = find_quad(list, SKIP_BUTTON_COLOR);
						if (!skip)
						{
							failure = "the playing intro shows no Skip button";
							return UiResult::Failed;
						}
						click_at(center(*skip));
						m_UiStep = UiStep::IntroSkipped;
					}
					else
					{
						// near the end: the video plays out on its own in a moment
						video->seek(key, std::max(0.0, video->duration(key) - 0.4));
						m_UiStep = UiStep::IntroEnded;
					}
					m_UiStepFrames = 0;
				}
				else if (m_UiStepFrames > 900)
				{
					failure = std::string(replay ? "the intro did not play again" : "the intro did not play") + " (state " + std::to_string(static_cast<int>(state)) + ", widget " + (intro->Visible ? "shown" : "hidden") +
						", picture in the HUD " + (picture ? "yes" : "no") + ", time " + std::to_string(video->time(key)) + " s): is Intro.mp4 imported and is there a decoder?";
					return UiResult::Failed;
				}
				return UiResult::Running;
			}

			if (m_UiStep == UiStep::IntroSkipped)
			{
				if (state == VideoState::Idle && !intro->Visible && !picture)
				{
					const std::optional<UiRect> button = find_quad(list, INTRO_BUTTON_COLOR);
					if (!button)
					{
						failure = "the HUD shows no Play intro button after the skip";
						return UiResult::Failed;
					}
					click_at(center(*button));
					m_UiStep = UiStep::IntroReplay;
					m_UiStepFrames = 0;
				}
				else if (m_UiStepFrames > 300)
				{
					failure = std::string("clicking Skip did not stop the intro and hide it (state ") + std::to_string(static_cast<int>(state)) + ", widget " + (intro->Visible ? "shown" : "hidden") + ")";
					return UiResult::Failed;
				}
				return UiResult::Running;
			}

			// IntroEnded
			if (state == VideoState::Ended && !intro->Visible && !picture)
			{
				m_UiStep = UiStep::Finish;
				m_UiStepFrames = 0;
			}
			else if (m_UiStepFrames > 900)
			{
				failure = std::string("the intro did not end and hide itself (state ") + std::to_string(static_cast<int>(state)) + ", widget " + (intro->Visible ? "shown" : "hidden") + ", time " + std::to_string(video->time(key)) + " s)";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case UiStep::Finish:
			return UiResult::Passed;
		}
		return UiResult::Passed;
	}

	void DemoSmoke::restore_input()
	{
		restore_ui();
		if (!m_InputTouched)
		{
			return;
		}
		m_InputTouched = false;
		if (InputModule* input = Engine::get().get_module<InputModule>())
		{
			input->set_backend(nullptr);
			input->actions().cancel_listen();
			input->actions().reset_all();
		}
		std::error_code error;
		std::filesystem::remove(INPUT_OVERRIDES_FILE, error); // the rebind of the check was saved: it must not stay
	}

	// The input demo: the "Input demo" ball moves with D and jumps with Space (keys made up by a scripted backend), "Rebind Jump" listens for the
	// next key (a click through the scripted UI input) and the key it hears becomes the Jump binding, in the actions and on the HUD, and is saved.
	DemoSmoke::UiResult DemoSmoke::run_input_step(std::string& failure)
	{
		InputModule* input = Engine::get().get_module<InputModule>();
		UiModule* ui = Engine::get().get_module<UiModule>();
		Scene& play = *m_Play.play_scene();
		const entt::entity ball = find_named(play, "Input demo");
		const entt::entity hud = find_named(play, "Hud");
		if (input == nullptr || ui == nullptr || ball == entt::null || hud == entt::null)
		{
			failure = "the demo scene has no input demo (an entity Input demo with input_demo.lua, and the Hud)";
			return UiResult::Failed;
		}
		const EntityGuid hudGuid = play.registry().get<Id>(hud).Guid;
		const auto position = [&]() { return play.registry().get<Transform>(ball).Position; };
		const auto jump_state = [&]() { return input->actions().state("Gameplay", "Jump"); };
		m_InputStepFrames++;

		switch (m_InputStep)
		{
		case InputStep::Setup:
		{
			// a backend of our own (the editor leaves the capture to it), the UI fed by scripted input, and the default bindings
			input->set_backend(&m_InputBackend);
			input->set_capture(true);
			input->actions().reset_all();
			std::error_code error;
			std::filesystem::remove(INPUT_OVERRIDES_FILE, error);
			m_InputTouched = true;

			m_UiPreviousInput = ui->input_source();
			ui->set_canvas_override(HUD_CANVAS);
			ui->set_input_source(&ui->scripted_input());
			ui->scripted_input().clear_pointer();
			m_UiTouched = true;
			m_InputStep = InputStep::WaitReady;
			m_InputStepFrames = 0;
			return UiResult::Running;
		}

		case InputStep::WaitReady:
		{
			UiPanelInstance* instance = ui->instance_for(play, hudGuid);
			UiDrawList list;
			if (instance != nullptr && instance->ready())
			{
				play.update_world_transforms();
				ui->build_draw_list(play, HUD_CANVAS, list);
			}
			const std::optional<UiRect> button = find_quad(list, REBIND_COLOR);
			const std::optional<UiId> jumpText = instance != nullptr ? instance->find("InputInfo/JumpKey") : std::nullopt;
			const bool shown = jumpText && instance->data().find(*jumpText)->Text == "Jump: Key.Space, Pad.A";

			if (!button || !shown || !input->actions().map_active("Gameplay") || position().y > 0.6f)
			{
				if (m_InputStepFrames > 1500)
				{
					failure = std::string("the input demo did not start: ") + (instance == nullptr || !instance->ready() ? "the HUD is not loaded" : !button ? "no Rebind Jump button" :
						!input->actions().map_active("Gameplay") ? "the Gameplay map was not pushed (is input.wsettings pointing at assets/input/demo.winput, and did input_demo.lua start?)" :
						!shown ? "the HUD does not show the Jump bindings" : "the ball is not on the ground");
					return UiResult::Failed;
				}
				return UiResult::Running;
			}

			m_RebindCenter = (button->Min + button->Max) * 0.5f;
			m_InputStart = position();
			m_InputBackend.key(Key::D, true);
			m_InputStep = InputStep::Move;
			m_InputStepFrames = 0;
			return UiResult::Running;
		}

		case InputStep::Move:
		{
			if (position().x > m_InputStart.x + 0.5f)
			{
				m_InputBackend.key(Key::D, false);
				m_InputBackend.key(Key::Space, true);
				m_InputStep = InputStep::Jump;
				m_InputStepFrames = 0;
			}
			else if (m_InputStepFrames > 1500)
			{
				failure = "holding D did not move the Input demo ball to the right (x " + std::to_string(position().x) + ", started at " + std::to_string(m_InputStart.x) + ")";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case InputStep::Jump:
		{
			if (m_InputStepFrames == 1)
			{
				m_InputBackend.key(Key::Space, false);
			}
			if (position().y > m_InputStart.y + 0.3f)
			{
				// click Rebind Jump through the UI
				ui->scripted_input().move_to(m_RebindCenter);
				ui->scripted_input().press(0);
				ui->scripted_input().release(0);
				m_InputStep = InputStep::RebindClick;
				m_InputStepFrames = 0;
			}
			else if (m_InputStepFrames > 1500)
			{
				failure = "pressing Space did not make the Input demo ball jump (y " + std::to_string(position().y) + ", started at " + std::to_string(m_InputStart.y) + ")";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case InputStep::RebindClick:
		{
			if (input->actions().listening())
			{
				m_InputBackend.key(Key::E, true);
				m_InputStep = InputStep::Heard;
				m_InputStepFrames = 0;
			}
			else if (m_InputStepFrames > 600)
			{
				failure = "clicking Rebind Jump did not start listening for a key";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case InputStep::Heard:
		{
			if (m_InputStepFrames == 1)
			{
				m_InputBackend.key(Key::E, false);
			}
			const wiwa::Action* jump = input->actions().action("Gameplay", "Jump");
			if (jump != nullptr && !jump->Bindings.empty() && jump->Bindings[0].Parts[0] == control(Key::E))
			{
				m_InputBackend.key(Key::Space, true);
				m_InputStep = InputStep::OldKey;
				m_InputStepFrames = 0;
			}
			else if (m_InputStepFrames > 600)
			{
				failure = "pressing E after Rebind Jump did not bind Jump to E";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}

		case InputStep::OldKey:
		{
			// one frame later the old key is read: it must not trigger Jump any more
			if (m_InputStepFrames == 3)
			{
				if (jump_state().Down)
				{
					failure = "Jump still fires on Space after it was rebound to E";
					return UiResult::Failed;
				}
				m_InputBackend.key(Key::Space, false);
				m_InputBackend.key(Key::E, true);
				m_InputStep = InputStep::NewKey;
				m_InputStepFrames = 0;
			}
			return UiResult::Running;
		}

		case InputStep::NewKey:
		{
			if (m_InputStepFrames == 3)
			{
				if (!jump_state().Down)
				{
					failure = "Jump does not fire on E after it was rebound to it";
					return UiResult::Failed;
				}
				m_InputBackend.key(Key::E, false);
				m_InputStep = InputStep::Saved;
				m_InputStepFrames = 0;
			}
			return UiResult::Running;
		}

		case InputStep::Saved:
		{
			UiPanelInstance* instance = ui->instance_for(play, hudGuid);
			const std::optional<UiId> jumpText = instance != nullptr ? instance->find("InputInfo/JumpKey") : std::nullopt;
			const bool shown = jumpText && instance->data().find(*jumpText)->Text == "Jump: Key.E, Pad.A";
			std::error_code error;
			if (shown && std::filesystem::exists(INPUT_OVERRIDES_FILE, error))
			{
				return UiResult::Passed;
			}
			if (m_InputStepFrames > 600)
			{
				failure = shown ? "the rebind was not saved to input_overrides.json" : "the HUD does not show Key.E as a Jump binding";
				return UiResult::Failed;
			}
			return UiResult::Running;
		}
		}
		return UiResult::Passed;
	}

	// One frame of the joints demo (door on a hinge, cart on a slider, spring pendulum, bridge, box that breaks away): what the Joints phase judges.
	bool DemoSmoke::sample_physics(Scene& play, std::string& failure)
	{
		const auto transform_of = [&](const std::string& name) -> const Transform*
		{
			const entt::entity entity = find_named(play, name);
			return entity == entt::null ? nullptr : &play.registry().get<Transform>(entity);
		};
		const Transform* door = transform_of("Door");
		const Transform* cart = transform_of("Slider cart");
		const Transform* bob = transform_of("Pendulum bob");
		const Transform* weak = transform_of("Weak box");
		const Transform* patrol = transform_of("Patrol");
		const Transform* platform = transform_of("Sliding platform");
		const Transform* links[5];
		bool linksFound = true;
		for (int i = 0; i < 5; i++)
		{
			links[i] = transform_of("Bridge link " + std::to_string(i));
			linksFound = linksFound && links[i] != nullptr;
		}
		if (door == nullptr || cart == nullptr || bob == nullptr || weak == nullptr || !linksFound || patrol == nullptr || platform == nullptr)
		{
			failure = std::string("the joints and character demo entities are missing in the running scene (patrol ") + (patrol ? "yes" : "NO") + ", platform " + (platform ? "yes" : "NO") + ", door " + (door ? "yes" : "NO") + ", cart " + (cart ? "yes" : "NO") +
				", bob " + (bob ? "yes" : "NO") + ", weak box " + (weak ? "yes" : "NO") + ", bridge " + (linksFound ? "yes" : "NO") + ")";
			return false;
		}

		PhysicsWatch& watch = m_Watch;
		watch.DoorSwing = std::max(watch.DoorSwing, std::abs(door->Rotation.y));
		watch.CartLow = std::min(watch.CartLow, cart->Position.x);
		watch.CartHigh = std::max(watch.CartHigh, cart->Position.x);
		watch.BobPath += watch.BobSeen ? glm::length(bob->Position - watch.BobLast) : 0.0f;
		watch.BobLast = bob->Position;
		watch.BobSeen = true;
		watch.WeakLow = std::min(watch.WeakLow, weak->Position.y);
		watch.PatrolLowX = std::min(watch.PatrolLowX, patrol->Position.x);
		watch.PatrolHighX = std::max(watch.PatrolHighX, patrol->Position.x);
		watch.PatrolLowY = std::min(watch.PatrolLowY, patrol->Position.y);
		watch.PatrolHighY = std::max(watch.PatrolHighY, patrol->Position.y);
		watch.PlatformLow = std::min(watch.PlatformLow, platform->Position.x);
		watch.PlatformHigh = std::max(watch.PlatformHigh, platform->Position.x);
		for (int i = 0; i < 5; i++)
		{
			watch.BridgeLow = std::min(watch.BridgeLow, links[i]->Position.y);
			if (i > 0)
			{
				watch.BridgeStretch = std::max(watch.BridgeStretch, glm::length(links[i]->Position - links[i - 1]->Position));
			}
		}
		return true;
	}

	// The end of a good run: Stop, remove the files the test made, and see that nothing was logged as an error.
	std::string DemoSmoke::finish_checks()
	{
		m_Play.stop();

		remove_hot_script();

		std::ifstream log("wiwa.log");
		log.seekg(static_cast<std::streamoff>(m_LogStart));
		std::string line;
		std::string errors;
		while (std::getline(log, line))
		{
			if (line.find("[error]") != std::string::npos && line.find("zz_hot_smoke") == std::string::npos)
			{
				errors += line + "\n";
			}
		}
		return errors.empty() ? std::string() : "errors were logged:\n" + errors;
	}
}
