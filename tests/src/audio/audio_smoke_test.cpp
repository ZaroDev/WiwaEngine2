#include <editor_common.h>
#include "audio_smoke_test.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <fstream>
#include <sstream>

#include <assets/wasset_io.h>
#include <assets/asset_manager.h>
#include <audio/audio_bank_info.h>
#include <audio/audio_module.h>
#include <audio/audio_project_settings.h>
#include <audio/mini/mini_backend.h>
#include <audio/null_backend.h>
#include <core/engine.h>
#include <scene/scene.h>
#include <scripting/script_instances.h>
#include <scripting/script_runtime.h>
#include <scripting/script_source.h>
#include <asset_pipeline/lua_stubs.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/audio_settings_file.h>
#include <audio/audio_edit_model.h>
#include <settings/audio_section.h>

#define WI_AUDIO_LOG(level, ...) WI_LOG(level, "Audio", __VA_ARGS__)

#include <test_kit.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Audio";
			static constexpr const char* Prefix = "Audio smoke test";
		};
		using Check = BasicCheck<CheckLog>;

		bool has_call(const NullAudioBackend& backend, const std::string& call)
		{
			return std::find(backend.calls().begin(), backend.calls().end(), call) != backend.calls().end();
		}

		// The calls that contain the text (for ones that carry an id the test cannot know).
		u32 count_containing(const NullAudioBackend& backend, const std::string& text)
		{
			return static_cast<u32>(std::count_if(backend.calls().begin(), backend.calls().end(), [&](const std::string& call) { return call.find(text) != std::string::npos; }));
		}

		u32 count_calls(const NullAudioBackend& backend, const std::string& prefix)
		{
			u32 count = 0;
			for (const std::string& call : backend.calls())
			{
				if (call.rfind(prefix, 0) == 0)
				{
					count++;
				}
			}
			return count;
		}

		NullAudioBackend& null_of(AudioModule& module)
		{
			return static_cast<NullAudioBackend&>(module.backend());
		}

		void null_backend(Check& check)
		{
			AudioModule module;
			module.init();
			check(module.active_kind() == AudioBackendKind::Null, "null_active: the module starts on the Null backend");

			NullAudioBackend& backend = null_of(module);
			const EmitterHandle emitter = backend.create_emitter("e");
			check(emitter.valid() && backend.has_emitter(emitter), "null_emitter: an emitter is created");

			const PlayingId playing = backend.post_event("Play_Test", emitter);
			check(playing.valid() && has_call(backend, "post:Play_Test@" + std::to_string(emitter.Value)), "null_post_logs: a posted event is recorded with its emitter");
			check(backend.playing_count() == 1, "null_playing: one event plays");

			backend.stop_all();
			check(backend.playing_count() == 0, "null_stop_all: nothing plays after stop_all");

			backend.set_rtpc("Volume", 0.5f, emitter);
			backend.set_switch("Surface", "Grass", emitter);
			backend.set_state("Mood", "Calm");
			check(has_call(backend, "rtpc:Volume=0.5@" + std::to_string(emitter.Value)), "null_rtpc: an RTPC is recorded");
			check(has_call(backend, "switch:Surface=Grass@" + std::to_string(emitter.Value)), "null_switch: a switch is recorded");
			check(has_call(backend, "state:Mood=Calm"), "null_state: a state is recorded");

			backend.destroy_emitter(emitter);
			check(!backend.has_emitter(emitter), "null_destroy: a destroyed emitter is gone");

			module.update(0.016f);
			check(backend.update_count() == 1, "module_update: the module gives the backend one update per frame");
			module.shutdown();
		}

		void unknown_event(Check& check)
		{
			AudioModule module;
			module.init();
			NullAudioBackend& backend = null_of(module);
			backend.set_strict(true);
			const std::string known[] = { "Play_Known" };
			backend.note_bank_events("Test", known);

			check(backend.post_event("Play_Known", {}).valid(), "unknown_event: a known event starts");
			check(!backend.post_event("Play_Typo", {}).valid(), "unknown_event: an unknown event does not start");
			backend.post_event("Play_Typo", {});
			check(backend.unknown_warnings() == 1, "unknown_event_once: an unknown name is warned about once");
			module.shutdown();
		}

		// What Wwise writes (trimmed): numbers as strings, two banks, a looping and a one-shot event.
		constexpr const char* INFO_JSON = R"({"SoundBanksInfo":{"SoundBanks":[
			{"ShortName":"Init","Events":[]},
			{"ShortName":"Test",
			 "Events":[
			   {"Id":"1","Name":"Play_Test","MaxAttenuation":"100","DurationType":"OneShot","DurationMin":"1.5","DurationMax":"2.0"},
			   {"Id":"2","Name":"Play_TestLoop","MaxAttenuation":50,"DurationType":"Infinite"}],
			 "GameParameters":[{"Id":"3","Name":"TestVolume"}],
			 "SwitchGroups":[{"Id":"4","Name":"TestSwitch","GroupValues":[{"Id":"5","Name":"A"},{"Id":"6","Name":"B"}]}],
			 "StateGroups":[{"Id":"7","Name":"TestState","States":[{"Id":"8","Name":"None"},{"Id":"9","Name":"Calm"}]}],
			 "Buses":[{"Id":"10","Name":"Master Audio Bus"}]}]}})";

		void bank_info(Check& check)
		{
			std::string error;
			const std::optional<AudioBankInfo> info = parse_soundbanks_info(INFO_JSON, "Test", error);
			check(info.has_value(), "info_parse: the file parses");
			if (info)
			{
				check(info->Events.size() == 2 && info->Rtpcs.size() == 1 && info->Switches.size() == 1 && info->States.size() == 1 && info->Buses.size() == 1,
					"info_parse: events, RTPCs, switch groups, state groups and buses are read");
				const AudioEventInfo* once = info->find_event("Play_Test");
				const AudioEventInfo* loop = info->find_event("Play_TestLoop");
				check(once && once->MaxAttenuation == 100.0f && once->DurationMs == 2000.0f && !once->Looping, "info_parse: a one-shot event's attenuation and duration (a numeric string)");
				check(loop && loop->MaxAttenuation == 50.0f && loop->Looping, "info_parse: an infinite event loops (a number)");
				check(info->Switches[0].Name == "TestSwitch" && info->Switches[0].Values.size() == 2, "info_parse: a switch group's values");
				check(info->States[0].Values.size() == 2, "info_parse: a state group's values");
			}

			check(parse_soundbanks_info("{ not json", "Test", error).has_value() == false && !error.empty(), "info_malformed: broken JSON gives nullopt and a reason");
			check(!parse_soundbanks_info(R"({"other":1})", "Test", error).has_value(), "info_malformed: a JSON without SoundBanksInfo is refused");
			check(!parse_soundbanks_info(INFO_JSON, "Missing", error).has_value(), "info_unlisted: a bank the file does not list gives nullopt");
			const std::optional<AudioBankInfo> merged = parse_soundbanks_info(INFO_JSON, "", error);
			check(merged && merged->Events.size() == 2, "info_merge: an empty bank name merges every bank");
		}

		void bank_asset(Check& check)
		{
			SoundBankData bank;
			bank.Name = "Test";
			bank.Bytes = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
			std::string error;
			bank.Info = *parse_soundbanks_info(INFO_JSON, "Test", error);
			bank.HasInfo = true;

			const std::vector<std::byte> payload = serialize(bank);
			const std::optional<SoundBankData> back = deserialize<SoundBankData>(payload);
			check(back && back->Name == "Test" && back->Bytes == bank.Bytes && back->HasInfo && back->Info == bank.Info, "bank_roundtrip: name, bytes and metadata survive serialize and load");

			SoundBankData bare;
			bare.Name = "Init";
			bare.Bytes = { std::byte{ 9 } };
			const std::optional<SoundBankData> bareBack = deserialize<SoundBankData>(serialize(bare));
			check(bareBack && !bareBack->HasInfo && bareBack->Bytes.size() == 1, "bank_no_info: a bank without metadata round trips");

			std::vector<std::byte> truncated(payload.begin(), payload.end() - 3);
			check(!deserialize<SoundBankData>(truncated).has_value(), "bank_truncated: a cut payload is refused");
			SoundBankData empty;
			empty.Name = "Empty";
			check(!deserialize<SoundBankData>(serialize(empty)).has_value(), "bank_empty: a bank without bytes is refused");
		}

		void bank_in_module(Check& check)
		{
			AudioModule module;
			module.init();
			NullAudioBackend& backend = null_of(module);
			backend.set_strict(true);

			SoundBankData bank;
			bank.Name = "Test";
			bank.Bytes = { std::byte{ 1 } };
			std::string error;
			bank.Info = *parse_soundbanks_info(INFO_JSON, "Test", error);
			bank.HasInfo = true;

			check(module.load_bank_data(bank, AssetRef{}, error), "bank_module: the backend takes the bank");
			check(has_call(backend, "load_bank:Test") && module.banks().size() == 1, "bank_module: the load reaches the backend and is listed");
			check(module.event_known("Play_Test") && !module.event_known("Play_Typo"), "bank_module: event_known follows the metadata");
			check(module.bank_infos().size() == 1, "bank_module: the metadata is offered to the editor");
			check(backend.post_event("Play_Test", {}).valid(), "unknown_event_after_bank: a known event starts once its bank is loaded");
			check(!backend.post_event("Play_Typo", {}).valid() && backend.unknown_warnings() == 1, "unknown_event_after_bank: an unknown one does not and warns once");

			module.unload_banks();
			check(module.banks().empty() && has_call(backend, "unload_bank:Test"), "bank_unload: unloading reaches the backend");
			module.shutdown();
		}

		// An entity at (1, 2, 3) with an AudioEmitter.
		entt::entity add_emitter(Scene& scene, const std::string& name, const std::string& event, bool playOnStart)
		{
			const entt::entity entity = scene.create(name);
			scene.registry().get<Transform>(entity).Position = glm::vec3(1.0f, 2.0f, 3.0f);
			AudioEmitter& emitter = scene.registry().emplace<AudioEmitter>(entity);
			emitter.Event = event;
			emitter.PlayOnStart = playOnStart;
			scene.update_world_transforms();
			return entity;
		}

		void world_sync(Check& check)
		{
			AudioModule module;
			module.init();
			module.set_auto_start(false);
			NullAudioBackend& backend = null_of(module);

			Ref<Scene> scene = CreateRef<Scene>();
			const entt::entity entity = add_emitter(*scene, "Lamp", "Play_Test", true);

			check(module.start_world(scene), "world_start: a world starts for a scene");
			module.tick(nullptr, true);
			const EmitterHandle emitter = module.emitter_of(entity);
			check(emitter.valid() && count_calls(backend, "create_emitter:Lamp") == 1, "emitter_created: one emitter is created for the component");
			check(backend.emitter_transform(emitter).Position == AudioVec3{ 1.0f, 2.0f, 3.0f }, "emitter_position: the emitter is where the entity is");
			check(backend.emitter_transform(emitter).Forward == AudioVec3{ 0.0f, 0.0f, -1.0f }, "emitter_forward: an unrotated entity faces -Z");
			check(count_calls(backend, "post:Play_Test@") == 1, "play_on_start: the event is posted once");

			module.tick(nullptr, true);
			module.tick(nullptr, true);
			check(count_calls(backend, "post:Play_Test@") == 1, "play_on_start_once: later frames do not post it again");
			check(count_calls(backend, "set_emitter:") == 1, "emitter_still: a still entity sends no transform again");

			scene->registry().get<Transform>(entity).Position = glm::vec3(4.0f, 2.0f, 3.0f);
			scene->update_world_transforms();
			module.tick(nullptr, true);
			check(count_calls(backend, "set_emitter:") == 2 && backend.emitter_transform(emitter).Position.X == 4.0f, "emitter_moves: a moved entity sends its new transform once");

			scene->registry().get<AudioEmitter>(entity).AttenuationScale = 2.0f;
			module.tick(nullptr, true);
			check(count_calls(backend, "scale:") == 2, "emitter_scale: a changed scale is sent");

			// Pause: nothing is synced and the backend is suspended; resuming lifts it.
			backend.clear_calls();
			scene->registry().get<Transform>(entity).Position = glm::vec3(9.0f, 2.0f, 3.0f);
			scene->update_world_transforms();
			module.tick(nullptr, false);
			check(backend.paused() && count_calls(backend, "set_emitter:") == 0, "paused_clock: a paused game suspends the backend and syncs nothing");
			module.tick(nullptr, true);
			check(!backend.paused() && count_calls(backend, "set_emitter:") == 1, "paused_clock: resuming lifts the pause and syncs");

			// The entity goes: its emitter and its sound go with it.
			check(backend.playing_count() == 1, "entity_destroyed_stops: the event plays before the entity goes");
			scene->destroy(entity);
			module.tick(nullptr, true);
			check(!backend.has_emitter(emitter) && backend.playing_count() == 0 && module.emitter_of(entity) == EmitterHandle{}, "entity_destroyed_stops: the emitter and its sound are gone");

			// The component goes, the entity stays.
			const entt::entity other = add_emitter(*scene, "Other", "Play_Test", true);
			module.tick(nullptr, true);
			const EmitterHandle otherEmitter = module.emitter_of(other);
			scene->registry().remove<AudioEmitter>(other);
			module.tick(nullptr, true);
			check(otherEmitter.valid() && !backend.has_emitter(otherEmitter), "component_removed: removing the component destroys its emitter");

			module.shutdown();
		}

		void world_lifecycle(Check& check)
		{
			// Stop silences everything, and a clone world leaves the base scene alone (the editor Play runs on a clone).
			AudioModule module;
			module.init();
			module.set_auto_start(false);
			NullAudioBackend& backend = null_of(module);

			Ref<Scene> edit = CreateRef<Scene>();
			add_emitter(*edit, "Edit", "Play_TestLoop", false);
			Ref<Scene> clone = CreateRef<Scene>();
			const entt::entity a = add_emitter(*clone, "A", "Play_TestLoop", false);
			add_emitter(*clone, "B", "Play_TestLoop", false);

			module.start_world(clone);
			module.tick(nullptr, true);
			check(backend.emitter_count() == 2, "clone_registers: the two emitters of the clone register");
			backend.post_event("Play_TestLoop", module.emitter_of(a));
			check(backend.playing_count() == 1, "stop_world: a looping event plays");

			module.stop_world();
			check(backend.playing_count() == 0 && backend.emitter_count() == 0 && module.world_scene() == nullptr, "stop_world_silences: Stop leaves no sound and no emitter");
			check(edit->registry().view<AudioEmitter>().size() == 1, "clone_unregisters: the edit scene keeps its emitter");
			module.tick(nullptr, true);
			check(backend.emitter_count() == 0, "stop_world_silences: without a world nothing registers");
			module.shutdown();
		}

		void world_follows_scene(Check& check)
		{
			AudioModule module; // auto start: the world follows the active scene while the clock plays
			module.init();
			NullAudioBackend& backend = null_of(module);

			Ref<Scene> first = CreateRef<Scene>();
			const entt::entity a = add_emitter(*first, "First", "Play_Test", false);
			Ref<Scene> second = CreateRef<Scene>();
			add_emitter(*second, "Second", "Play_Test", false);

			module.tick(first, false);
			check(module.world_scene() == nullptr && backend.emitter_count() == 0, "auto_idle: nothing registers while the clock is stopped");
			module.tick(first, true);
			const EmitterHandle firstEmitter = module.emitter_of(a);
			check(module.world_scene() == first && firstEmitter.valid(), "auto_start: the active scene gets a world once the clock plays");

			module.tick(second, true);
			check(!backend.has_emitter(firstEmitter) && module.world_scene() == second && backend.emitter_count() == 1, "scene_replaced: the emitters of the old scene go, those of the new one register");

			module.tick(nullptr, false);
			check(module.world_scene() == nullptr && backend.emitter_count() == 0, "auto_stop: Stop (the active scene changes) silences the world");

			first.reset();
			second.reset();
			module.shutdown();
		}

		void listener(Check& check)
		{
			AudioModule module;
			module.init();
			module.set_auto_start(false);
			NullAudioBackend& backend = null_of(module);

			Ref<Scene> scene = CreateRef<Scene>();
			module.start_world(scene);
			module.tick(nullptr, true);
			check(count_calls(backend, "listener") == 0, "listener_none: no listener and no camera: nothing is sent");

			const entt::entity camera = scene->create("Camera");
			scene->registry().emplace<Camera>(camera).Primary = true;
			scene->registry().get<Transform>(camera).Position = glm::vec3(0.0f, 5.0f, 0.0f);
			scene->update_world_transforms();
			module.tick(nullptr, true);
			check(count_calls(backend, "listener") == 1 && backend.listener().Position.Y == 5.0f, "listener_camera: the primary camera is the listener without an AudioListener");

			const entt::entity ears = scene->create("Ears");
			scene->registry().emplace<AudioListener>(ears);
			scene->registry().get<Transform>(ears).Position = glm::vec3(0.0f, 7.0f, 0.0f);
			scene->update_world_transforms();
			module.tick(nullptr, true);
			check(backend.listener().Position.Y == 7.0f, "listener_component: an active AudioListener wins over the camera");

			scene->registry().get<AudioListener>(ears).Active = false;
			module.tick(nullptr, true);
			check(backend.listener().Position.Y == 5.0f, "listener_inactive: an inactive AudioListener falls back to the camera");
			module.shutdown();
		}

		class NoScripts final : public IScriptSource
		{
		public:
			virtual std::optional<LoadedScript> find_by_path(std::string_view) override { return std::nullopt; }
			virtual std::optional<LoadedScript> find_by_asset(AssetRef) override { return std::nullopt; }
		};

		// A Lua runtime with the Audio API bound to a module on the Null backend and a scene with one emitter, Lamp.
		struct LuaAudioHarness
		{
			NoScripts Source;
			ScriptRuntime Runtime{ Source };
			AudioModule Module;
			Ref<Scene> TheScene = CreateRef<Scene>();
			ScriptInstances Instances{ Runtime };
			sol::environment Env{ Runtime.state(), sol::create, Runtime.sandbox() };

			LuaAudioHarness()
			{
				Module.init();
				Module.set_auto_start(false);
				add_emitter(*TheScene, "Lamp", "", false);
				TheScene->create("Bare");
				Module.start_world(TheScene);
				Instances.context().Audio = &Module;
				Instances.context().ActiveScene = TheScene.get();
				Instances.context().ActiveSceneRef = TheScene;
			}

			NullAudioBackend& backend() { return null_of(Module); }

			std::string run(const std::string& chunk, std::string* error = nullptr)
			{
				sol::protected_function_result result = Runtime.state().safe_script(chunk, Env, sol::script_pass_on_error);
				if (!result.valid())
				{
					sol::error failure = result;
					if (error) *error = failure.what();
					return {};
				}
				if (error) error->clear();
				return result.get_type() == sol::type::none ? std::string() : Runtime.state()["tostring"](result.get<sol::object>()).get<std::string>();
			}

			bool fails(const std::string& chunk, const std::string& mention)
			{
				std::string error;
				run(chunk, &error);
				return !error.empty() && error.find(mention) != std::string::npos;
			}
		};

		void lua_audio(Check& check)
		{
			{
				LuaAudioHarness h;
				check(h.run("return Audio.backend()") == "Null", "lua_backend: the backend name");
				check(h.run("return Audio.post('Play_Test'):is_valid()") == "true" && has_call(h.backend(), "post:Play_Test@0"), "lua_post: a global post reaches the backend");
				check(h.run("return Audio.post('Play_Test', find_entity('Lamp')):is_valid()") == "true", "lua_post_entity: a post from an entity works before the first audio frame");
				check(count_calls(h.backend(), "create_emitter:Lamp") == 1 && count_calls(h.backend(), "post:Play_Test@1") == 1, "lua_post_entity: the emitter is made on the spot and used");
				h.Module.tick(nullptr, true);
				check(count_calls(h.backend(), "create_emitter:Lamp") == 1, "lua_post_entity: the next frame does not make it twice");
				h.backend().clear_calls();
				h.backend().clear_calls();
				check(h.run("return Audio.post('Play_Test', { loop = true, volume = 0.5 }):is_valid()") == "true"
					&& count_containing(h.backend(), "loop=true volume=0.5") == 1, "lua_post_options: loop and volume reach the backend (the table may come second)");
				check(h.run("return Audio.post('Play_Test', find_entity('Lamp'), { volume = 0.25 }):is_valid()") == "true" && count_containing(h.backend(), "loop=default volume=0.25") == 1,
					"lua_post_options_entity: an entity and the options together");
				h.run("local a = Audio.post('Play_Test'); Audio.set_volume(a, 0.75)");
				check(count_containing(h.backend(), "=0.75") == 1, "lua_set_volume: the gain of a playing sound changes");
				check(h.fails("Audio.post('Play_Test', { volume = -1 })", "volume") && h.fails("Audio.post('Play_Test', { pan = 1 })", "pan") && h.fails("Audio.post('Play_Test', 5)", "entity or an options table"),
					"lua_post_options_bad: a negative volume, an unknown option and a number are errors");
				check(h.fails("Audio.set_volume(Audio.post('Play_Test'), 0/0)", "0 or more") && h.fails("Audio.set_volume(Audio.post('Play_Test'), -2)", "0 or more"), "lua_set_volume_bad: a non-finite or negative gain is an error");
			}

			{
				LuaAudioHarness h;
				h.backend().set_strict(true);
				check(h.run("return Audio.post('Play_Typo') == nil") == "true", "lua_post_unknown: an unknown event gives nil, not an error");
			}

			{
				LuaAudioHarness h;
				h.run("local a = Audio.post('Play_TestLoop'); local b = Audio.post('Play_TestLoop'); Audio.stop(a)");
				check(h.backend().playing_count() == 1, "lua_stop: stopping one handle leaves the other");
				h.run("Audio.stop_all()");
				check(h.backend().playing_count() == 0, "lua_stop_all: nothing plays");
				h.run("Audio.post('Play_TestLoop', find_entity('Lamp')); Audio.stop_entity(find_entity('Lamp'))");
				check(h.backend().playing_count() == 0, "lua_stop_entity: the entity emitter is silenced");
			}

			{
				LuaAudioHarness h;
				h.run("Audio.set_rtpc('Volume', 0.5); Audio.set_switch('Surface', 'Grass'); Audio.set_state('Mood', 'Calm')");
				check(has_call(h.backend(), "rtpc:Volume=0.5@0") && has_call(h.backend(), "switch:Surface=Grass@0") && has_call(h.backend(), "state:Mood=Calm"), "lua_params: RTPC, switch and state reach the backend");
				h.run("Audio.set_rtpc('Volume', 1, find_entity('Lamp'))");
				check(has_call(h.backend(), "rtpc:Volume=1@1"), "lua_params: an RTPC can target an entity");
			}

			{
				LuaAudioHarness h;
				check(h.fails("Audio.post(5)", "string") || h.fails("Audio.post(5)", "expected"), "lua_bad_args: a non-string event is a Lua error");
				check(h.fails("Audio.post('Play_Test', find_entity('Bare'))", "AudioEmitter"), "lua_no_emitter: an entity without an AudioEmitter is an error that names the component");
				h.Instances.context().Audio = nullptr;
				check(h.fails("Audio.post('Play_Test')", "audio module"), "lua_no_module: without an audio module the call is a Lua error");
			}

			{
				std::ifstream file("engine/lua/wiwa.lua", std::ios::binary);
				std::stringstream text;
				text << file.rdbuf();
				const std::string stub = text.str();
				check(declared_globals(stub).contains("Audio") && stub.find("function Audio.post(") != std::string::npos && stub.find("function Audio.set_rtpc(") != std::string::npos,
					"lua_stubs_have_audio: the static stub declares Audio and its calls");
			}
		}

		void editor_tooling(Check& check)
		{
			std::string error;
			const AudioBankInfo info = *parse_soundbanks_info(INFO_JSON, "Test", error);
			const std::vector<const AudioBankInfo*> banks{ &info };

			const std::vector<AudioEventRow> all = audio_event_rows(banks, "");
			check(all.size() == 2 && all[0].Name == "Play_Test" && all[1].Name == "Play_TestLoop", "browser_rows: every event, sorted by name");
			const std::vector<AudioEventRow> loop = audio_event_rows(banks, "LOOP");
			check(loop.size() == 1 && loop[0].Name == "Play_TestLoop" && loop[0].Looping, "browser_search: the search is a case-insensitive substring");
			check(audio_event_rows(banks, "zzz").empty(), "browser_search: no match gives no rows");
			AudioBankInfo twin = info;
			check(audio_event_rows({ &info, &twin }, "").size() == 2, "browser_rows: an event in two banks is listed once");

			check(audio_event_unknown("Play_Typo", banks) && !audio_event_unknown("Play_Test", banks), "picker_unknown_flagged: a typo is flagged, a known event is not");
			check(!audio_event_unknown("", banks) && !audio_event_unknown("Play_Typo", {}), "picker_unknown_flagged: an empty name, or no metadata at all, is never flagged");

			check(audio_attenuation_radius("Play_Test", 1.0f, banks) == 100.0f && audio_attenuation_radius("Play_Test", 2.0f, banks) == 200.0f, "gizmo_radius: the radius is the event attenuation times the emitter scale");
			check(audio_attenuation_radius("Play_Typo", 1.0f, banks) == 0.0f && audio_attenuation_radius("Play_Test", 1.0f, {}) == 0.0f, "gizmo_radius: an unknown event has no radius");

			// Audition: works in edit mode (no world), never while a scene plays, and stop silences it.
			AudioModule module;
			module.init();
			module.set_auto_start(false);
			NullAudioBackend& backend = null_of(module);
			check(module.audition("Play_Test").valid() && module.auditioning() && backend.playing_count() == 1, "audition_works_in_edit: an event plays without a world");
			module.stop_audition();
			check(!module.auditioning() && backend.playing_count() == 0, "audition_stop: stopping the audition silences it");
			check(!module.audition("").valid(), "audition_empty: an empty event name plays nothing");

			Ref<Scene> scene = CreateRef<Scene>();
			module.audition("Play_TestLoop");
			module.start_world(scene);
			check(!module.auditioning() && backend.playing_count() == 0, "audition_world_start: a world that starts ends the audition");
			check(!module.audition("Play_Test").valid(), "audition_blocked: no audition while a scene plays");
			module.shutdown();
		}

		void bank_order(Check& check)
		{
			SoundBankData test, ambience, init;
			test.Name = "Test";
			ambience.Name = "Ambience";
			init.Name = "Init";
			std::vector<const SoundBankData*> order{ &test, &ambience, &init };
			AudioModule::sort_init_first(order);
			check(order.size() == 3 && order[0] == &init && order[1] == &test && order[2] == &ambience, "bank_order: Init goes first and the others keep their order, whatever order they were listed in");
			std::vector<const SoundBankData*> noInit{ &test, &ambience };
			AudioModule::sort_init_first(noInit);
			check(noInit[0] == &test && noInit[1] == &ambience, "bank_order: without Init the order is kept");
		}

		void null_log_bounded(Check& check)
		{
			NullAudioBackend backend;
			const EmitterHandle emitter = backend.create_emitter("e");
			for (size_t i = 0; i < NullAudioBackend::MAX_CALLS * 3; i++)
			{
				backend.set_emitter(emitter, AudioTransform{});
			}
			check(backend.calls().size() <= NullAudioBackend::MAX_CALLS, "null_log_bounded: a long-running Null backend does not grow its call log without bound");
			backend.post_event("Play_Last", emitter);
			check(has_call(backend, "post:Play_Last@" + std::to_string(emitter.Value)), "null_log_bounded: the newest calls are kept");
		}

		void project_settings(Check& check)
		{
			AudioProjectSettings settings;
			settings.Backend = AudioBackendKind::Wwise;
			settings.Banks = { AssetRef{ AssetGuid{ 1, 2 }, 0 }, AssetRef{ AssetGuid{ 3, 4 }, 0 } };
			settings.MemoryPoolKb = 32768;
			settings.Language = "Spanish(Spain)";

			AudioProjectSettings fromText;
			std::string error;
			check(audio_settings_from_text(audio_settings_to_text(settings), fromText, error) && fromText == settings, "settings_roundtrip: the JSON file keeps every value");

			const std::optional<AudioProjectSettings> fromBlob = deserialize<AudioProjectSettings>(serialize(settings));
			check(fromBlob && *fromBlob == settings, "settings_pack: the packed blob keeps every value");

			std::vector<std::byte> blob = serialize(settings);
			std::vector<std::byte> cut(blob.begin(), blob.end() - 1);
			check(!deserialize<AudioProjectSettings>(cut).has_value(), "settings_pack: a cut blob is refused");
			blob[4] = std::byte{ 9 }; // the backend byte, after the u32 version
			check(!deserialize<AudioProjectSettings>(blob).has_value(), "settings_pack: an unknown backend is refused");

			AudioProjectSettings wild;
			wild.MemoryPoolKb = 1;
			wild.Language.clear();
			wild.Banks = { AssetRef{}, AssetRef{ AssetGuid{ 5, 6 }, 0 }, AssetRef{ AssetGuid{ 5, 6 }, 0 } };
			const AudioProjectSettings clean = sanitized(wild);
			check(clean.MemoryPoolKb == AUDIO_POOL_MIN_KB && !clean.Language.empty() && clean.Banks.size() == 1, "pool_sanitized: a tiny pool, no language, an invalid and a repeated bank are corrected");
			wild.MemoryPoolKb = 0xFFFFFFFFu;
			check(sanitized(wild).MemoryPoolKb == AUDIO_POOL_MAX_KB, "pool_sanitized: a huge pool is cut");

			check(audio_settings_from_text(R"({"backend":"FMOD"})", fromText, error) == false && !error.empty(), "settings_bad_backend: an unknown backend name is refused with a reason");
			check(audio_settings_from_text(R"({"banks":["nope"]})", fromText, error) == false, "settings_bad_guid: a bad bank guid is refused");
			check(audio_settings_from_text("[]", fromText, error) == false, "settings_not_object: a JSON list is refused");
			check(audio_settings_from_text(R"({"unknown":1})", fromText, error) && fromText == AudioProjectSettings{}, "settings_defaults: missing keys keep their defaults, unknown keys are ignored");

			// The page model: a change is saved and applied; Wwise without its SDK falls back to Null and says why.
			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_audio_smoke.wsettings";
			std::error_code removed;
			std::filesystem::remove(file, removed);
			{
				AudioSection section(file, false); // saved only: the running module belongs to the main thread, this check runs on another
				check(section.settings() == AudioProjectSettings{}, "section_default: no file gives the default settings");
				check(section.set_memory_pool(1), "section_set: a value is accepted and saved");
				check(section.settings().MemoryPoolKb == AUDIO_POOL_MIN_KB, "section_set: the value is sanitized");
				AudioSection reloaded(file, false);
				check(reloaded.settings().MemoryPoolKb == AUDIO_POOL_MIN_KB, "section_saved: a new section reads what was saved");
				check(section.remove_bank(5) == false, "section_remove: a missing bank index is refused");
			}
			std::filesystem::remove(file, removed);

#ifndef WI_AUDIO_WWISE
			// What the section applies, on a module of its own: Wwise without its SDK falls back to Null and says why; Null starts clean.
			if (AssetManager* assets = Engine::get().get_module<AssetManager>())
			{
				AudioModule module;
				module.init();
				AudioProjectSettings wwise;
				wwise.Backend = AudioBackendKind::Wwise;
				check(!module.apply_settings(*assets, wwise) && module.active_kind() == AudioBackendKind::Null && !module.last_error().empty(), "section_apply_selects_backend: Wwise without its SDK falls back to Null and says why");
				check(module.apply_settings(*assets, AudioProjectSettings{}) && module.last_error().empty(), "section_apply_selects_backend: Null starts without an error");
				module.shutdown();
			}
#endif
		}

#ifdef WI_AUDIO_WWISE
		std::optional<SoundBankData> read_test_bank(const std::string& name)
		{
			std::ifstream file("assets/audio/" + name + ".bnk", std::ios::binary);
			if (!file)
			{
				return std::nullopt;
			}
			std::stringstream text;
			text << file.rdbuf();
			const std::string bytes = text.str();
			SoundBankData bank;
			bank.Name = name;
			bank.Bytes.resize(bytes.size());
			std::memcpy(bank.Bytes.data(), bytes.data(), bytes.size());
			return bank.Bytes.empty() ? std::nullopt : std::optional<SoundBankData>(std::move(bank));
		}

		// The real sound engine. The banks are the ones the user generates in Wwise (editor/assets/audio: Init.bnk and Test.bnk with Play_Test, a short
		// one-shot); without them only the checks that need none run, and the rest are reported as skipped, never as passed.
		void wwise_checks(Check& check)
		{
			AudioModule module;
			module.init();
			module.set_auto_start(false);
			check(module.select_backend(AudioBackendKind::Wwise, AudioInitInfo{}), "wwise_init: the sound engine starts");
			check(module.active_kind() == AudioBackendKind::Wwise, "wwise_init: Wwise is the active backend");

			std::string error;
			SoundBankData garbage;
			garbage.Name = "Garbage";
			garbage.Bytes.assign(64, std::byte{ 0x7f });
			check(!module.load_bank_data(garbage, AssetRef{}, error) && !error.empty(), "wwise_bad_bank: bytes that are not a bank are refused with a reason");
			check(!module.backend().post_event("Play_DoesNotExist", {}).valid(), "wwise_unknown_event: an event no bank holds does not start");

			const std::optional<SoundBankData> init = read_test_bank("Init");
			const std::optional<SoundBankData> test = read_test_bank("Test");
			if (!init || !test)
			{
				WI_AUDIO_LOG(Warning, "SKIPPED: the Wwise playback checks: editor/assets/audio/Init.bnk and Test.bnk are missing");
			}
			else
			{
				check(module.load_bank_data(*init, AssetRef{}, error), "wwise_banks: Init loads: " + error);
				check(module.load_bank_data(*test, AssetRef{}, error), "wwise_banks: Test loads: " + error);
				const PlayingId playing = module.backend().post_event("Play_Test", {});
				check(playing.valid() && module.backend().playing_count() == 1, "wwise_post_counts: a posted event is counted as playing");

				// a short one-shot ends within a few seconds of rendered frames
				for (int frame = 0; frame < 300 && module.backend().playing_count() != 0; frame++)
				{
					module.tick(nullptr, true);
					std::this_thread::sleep_for(std::chrono::milliseconds(16));
				}
				check(module.backend().playing_count() == 0, "wwise_post_counts: the playing count falls back to zero when the event ends");
			}

			module.shutdown();
			check(module.banks().empty(), "wwise_shutdown_clean: shutdown unloads the banks");
		}
#endif

		// A mono 16-bit 48 kHz wav of a quiet tone.
		std::string make_wav(u32 samples)
		{
			std::string data;
			for (u32 i = 0; i < samples; i++)
			{
				const i16 value = static_cast<i16>(3000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / 48000.0));
				data.append(reinterpret_cast<const char*>(&value), 2);
			}
			auto put32 = [](std::string& out, u32 v) { out.append(reinterpret_cast<const char*>(&v), 4); };
			auto put16 = [](std::string& out, u16 v) { out.append(reinterpret_cast<const char*>(&v), 2); };
			std::string wav = "RIFF";
			put32(wav, 36 + static_cast<u32>(data.size()));
			wav += "WAVEfmt ";
			put32(wav, 16);
			put16(wav, 1);      // PCM
			put16(wav, 1);      // mono
			put32(wav, 48000);
			put32(wav, 96000);  // bytes per second
			put16(wav, 2);
			put16(wav, 16);
			wav += "data";
			put32(wav, static_cast<u32>(data.size()));
			return wav + data;
		}

		void mini_audio(Check& check, AssetPipelineModule& pipeline)
		{
			AudioClipData clip;
			clip.Name = "Shot";
			clip.Bytes = { std::byte{ 1 }, std::byte{ 2 } };
			const std::optional<AudioClipData> back = deserialize<AudioClipData>(serialize(clip));
			check(back && back->Name == "Shot" && back->Bytes == clip.Bytes, "clip_roundtrip: name and bytes survive serialize and load");
			AudioClipData empty;
			empty.Name = "Empty";
			check(!deserialize<AudioClipData>(serialize(empty)).has_value(), "clip_empty: a clip without bytes is refused");

			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (assets == nullptr)
			{
				check(false, "mini_assets: there is an asset manager");
				return;
			}

			// two sounds imported like any asset: a short one-shot and a looping one (the name says so)
			AssetDatabase& database = pipeline.get_database();
			std::error_code error;
			const std::filesystem::path dir = database.get_assets_root() / "zz_audio_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);
			write_text(dir / "zz_smoke_shot.wav", make_wav(2400));
			write_text(dir / "zz_smoke_hum_loop.wav", make_wav(4800));
			write_text(dir / "zz_smoke_fake.wav", "this is not a sound at all, only text");
			const std::optional<AssetRef> shot = database.ensure_imported(dir / "zz_smoke_shot.wav");
			const std::optional<AssetRef> hum = database.ensure_imported(dir / "zz_smoke_hum_loop.wav");
			WI_AUDIO_LOG(Info, "Smoke test: the next import error is expected");
			// A failed import still answers with the reference (the guid stays valid), so the proof is that nothing was cooked: the catalog, which the
			// editor writes when a scan has finished, lists the two sounds and not the fake one. The backend finds its clips there.
			database.ensure_imported(dir / "zz_smoke_fake.wav");
			database.scan();
			assets->refresh_catalog();
			check(!assets->find_asset("assets/zz_audio_smoke/zz_smoke_fake.wav").has_value(), "clip_import_bad: a file that is not a sound fails the import");
			check(shot && hum, "clip_import: wav files import as AudioClip assets");

			if (shot && hum)
			{
				MiniAudioBackend backend(assets, true); // no sound card: the engine renders when asked
				std::string failure;
				check(backend.init(AudioInitInfo{}, failure), "mini_init: the engine starts without a device: " + failure);

				check(!backend.post_event("zz_smoke_nothing", {}).valid(), "mini_unknown: an unknown event does not start");
				const PlayingId once = backend.post_event("ZZ_Smoke_Shot", {}); // any letter case
				check(once.valid() && backend.playing_count() == 1, "mini_post: a clip starts under its file name, in any letter case");

				// the clip loads in the background: updates until it plays, then render it to its end
				for (int i = 0; i < 300; i++)
				{
					backend.update();
					backend.render(480);
					if (backend.playing_count() == 0)
					{
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				check(backend.playing_count() == 0, "mini_ends: a one-shot is gone when it has played to its end");

				const PlayingId loop = backend.post_event("zz_smoke_hum_loop", {});
				for (int i = 0; i < 100; i++)
				{
					backend.update();
					backend.render(4800);
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				check(loop.valid() && backend.playing_count() == 1, "mini_loop: a clip named _loop keeps playing past its length");

				const EmitterHandle emitter = backend.create_emitter("e");
				backend.post_event("zz_smoke_hum_loop", emitter);
				backend.set_rtpc("Volume", 0.5f, emitter);
				backend.set_rtpc("Pitch", 2.0f, {});
				backend.set_rtpc("Nonsense", 1.0f, {});
				backend.set_emitter(emitter, AudioTransform{ { 3.0f, 0.0f, 0.0f } });
				check(backend.playing_count() == 2, "mini_emitter: a positioned clip plays beside the global one");
				backend.stop_emitter(emitter);
				check(backend.playing_count() == 1, "mini_stop_emitter: stopping an emitter leaves the others");
				backend.stop_playing(loop);
				check(backend.playing_count() == 0, "mini_stop: a stopped sound is gone");

				// options: a one-shot asked to loop keeps playing, a _loop clip asked not to loop ends, and a gain can change while it plays
				PostOptions looping;
				looping.Loop = true;
				looping.Volume = 0.5f;
				const PlayingId forced = backend.post_event_with("zz_smoke_shot", {}, looping);
				PostOptions oneShot;
				oneShot.Loop = false;
				backend.post_event_with("zz_smoke_hum_loop", {}, oneShot);
				for (int i = 0; i < 100; i++)
				{
					backend.update();
					backend.render(4800);
					backend.set_playing_volume(forced, 0.25f);
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				const bool forcedPlays = forced.valid() && backend.playing_count() == 1;
				backend.stop_playing(forced);
				check(forcedPlays && backend.playing_count() == 0, "mini_options: Loop=true keeps a one-shot playing, Loop=false lets a _loop clip end (nothing else is left)");

				backend.post_event("zz_smoke_hum_loop", {});
				backend.stop_all();
				check(backend.playing_count() == 0, "mini_stop_all: nothing plays");
				std::string ignored;
				check(backend.load_bank("x", {}, ignored) && backend.loaded_banks().empty(), "mini_banks: it has no banks, and loading one is harmless");
				backend.shutdown();
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		void backend_selection(Check& check)
		{
			AudioModule module;
			module.init();
			AudioInitInfo info;
			const bool ok = module.select_backend(AudioBackendKind::Wwise, info);
#ifdef WI_AUDIO_WWISE
			check(ok && module.active_kind() == AudioBackendKind::Wwise && module.last_error().empty(), "select_wwise: the Wwise backend starts when it is built in");
#else
			check(!ok, "select_wwise_falls_back: Wwise without its SDK reports failure");
			check(module.active_kind() == AudioBackendKind::Null, "select_wwise_falls_back: the Null backend runs instead");
			check(!module.last_error().empty(), "select_wwise_falls_back: the reason is kept for the panel");
#endif
			module.shutdown();
		}
	}

	bool run_audio_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		null_backend(check);
		unknown_event(check);
		bank_info(check);
		bank_asset(check);
		bank_in_module(check);
		world_sync(check);
		world_lifecycle(check);
		world_follows_scene(check);
		listener(check);
		lua_audio(check);
		editor_tooling(check);
		bank_order(check);
		null_log_bounded(check);
		project_settings(check);
		mini_audio(check, pipeline);
		backend_selection(check);
#ifdef WI_AUDIO_WWISE
		wwise_checks(check);
#endif

		if (check.Failures == 0)
		{
			WI_AUDIO_LOG(Info, "Audio smoke test passed");
		}
		return check.Failures == 0;
	}
}
