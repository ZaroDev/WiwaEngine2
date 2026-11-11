#include <editor_common.h>
#include "anim_check.h"
#include "anim_fixture.h"

#include <scripting/script_instances.h>
#include <scripting/script_runtime.h>
#include <scripting/script_source.h>

namespace wiwa
{
	namespace
	{
		class NoScripts final : public IScriptSource
		{
		public:
			virtual std::optional<LoadedScript> find_by_path(std::string_view) override { return std::nullopt; }
			virtual std::optional<LoadedScript> find_by_asset(AssetRef) override { return std::nullopt; }
		};

		// The Lua API running over an AnimationModule that plays a scene with one character ("hero") and one entity without an Animator ("plain").
		struct LuaAnimHarness
		{
			NoScripts Source;
			ScriptRuntime Runtime{ Source };
			Fixture Rig;
			AnimationModule Module;
			ScriptInstances Instances{ Runtime };
			Ref<Scene> Active = CreateRef<Scene>();
			entt::entity Hero = entt::null;
			sol::environment Env{ Runtime.state(), sol::create, Runtime.sandbox() };
			sol::table Owner = Runtime.state().create_table(); // the self that handlers registered from this harness belong to

			LuaAnimHarness()
			{
				Module.set_sources(Rig.Sources);
				Hero = Rig.make_character(*Active, "hero");
				Active->create("plain");
				Active->update_world_transforms();
				Instances.context().Animation = &Module;
				Instances.context().GamePlaying = true;
				Instances.start(Active);
				Instances.ui_handlers().set_current(&Owner);
			}

			// The hero plays the rig's graph (anim_fixture.h add_graph).
			void use_graph()
			{
				Rig.add_graph();
				Active->registry().get<Animator>(Hero).Graph = Rig.GraphRef;
			}

			~LuaAnimHarness() { Instances.ui_handlers().set_current(nullptr); }

			// One frame as the engine runs it: the scripts (which hear last frame's events), then the animation module.
			void frame(float seconds)
			{
				Instances.update(seconds);
				Module.tick(Active, seconds, true);
			}

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

			float hip() { return Module.instance_for(Hero)->pose().T[0].x; }
			float hand() { return Module.instance_for(Hero)->pose().T[1].x; }
		};

		bool near_equal(float a, float b)
		{
			return std::abs(a - b) < 1e-3f;
		}

		void animator_handles(AnimCheck& check)
		{
			LuaAnimHarness h;
			check(h.run("return Animation.animator(find_entity('plain')) == nil") == "true", "an entity without an Animator has no animator handle");
			check(h.run("return Animation.animator(find_entity('hero')) ~= nil") == "true", "an entity with one does");

			// scripts run before the module in a frame: the handle works from the first frame of the scene
			check(h.run("local a = Animation.animator(find_entity('hero')); return tostring(a.ready)") == "true", "a handle made before the first tick is ready (the instance is made on demand)");

			// the speed is the Animator's
			h.run("Animation.animator(find_entity('hero')).speed = 2");
			check(near_equal(h.Active->registry().get<Animator>(h.Hero).Speed, 2.0f) && h.run("return Animation.animator(find_entity('hero')).speed") == "2.0", "speed reads and writes the Animator");
			h.run("Animation.animator(find_entity('hero')).speed = 1");
		}

		void playback(AnimCheck& check)
		{
			LuaAnimHarness h;
			h.Rig.LateLoaded = true; // an unknown name is only known to be unknown once every clip has loaded
			h.run("a = Animation.animator(find_entity('hero'))");
			h.frame(0.1f);
			check(near_equal(h.hip(), 1.0f), "the Clip of the Animator plays on start (Idle)");

			h.run("a:play('Run', { loop = true, speed = 1.0 })");
			h.frame(0.5f);
			check(near_equal(h.hip(), 5.0f), "play by name");
			check(h.run("return a.playing") == "true" && near_equal(std::stof(h.run("return a.time")), 0.5f), "playing and time");

			h.run("a:crossfade('Idle', 1.0, { loop = true })");
			h.frame(0.5f);
			check(near_equal(h.hip(), 0.5f), "a crossfade is halfway after half its time (Run has looped to 0, Idle is at 1): " + std::to_string(h.hip()));
			h.frame(1.0f);
			check(near_equal(h.hip(), 1.0f), "and ends on the new one");

			h.run("a:stop(0)");
			h.frame(0.1f);
			check(near_equal(h.hip(), 0.0f) && h.run("return a.playing") == "false", "stop returns to the rest pose");

			check(h.fails("a:play('Nope')", "Nope") && h.fails("a:play('Nope')", "Idle") && h.fails("a:play('Nope')", "Run"), "an unknown clip is an error that lists the clips");
			check(h.fails("a:play(42)", "clip"), "a clip is a name or an asset");
		}

		void layers(AnimCheck& check)
		{
			LuaAnimHarness h;
			h.run("a = Animation.animator(find_entity('hero')); a:play('Idle')");
			h.run("up = a:layer('upper', 'Wave', { mask = 'hand', weight = 0.5, additive = false, loop = true })");
			h.frame(0.1f);
			check(near_equal(h.hand(), 3.0f) && near_equal(h.hip(), 1.0f), "a masked layer at half weight");
			h.run("up.weight = 1");
			h.frame(0.1f);
			check(near_equal(h.hand(), 5.0f) && h.run("return up.weight") == "1.0", "the weight is writable");
			h.run("up:stop(0)");
			check(h.run("return tostring(up.weight)") == "nil", "a stopped layer reads nil");
			h.run("up.weight = 1; up:stop(0); up:crossfade('Wave', 0.1)"); // ignored, no error
			h.frame(0.1f);
			check(near_equal(h.hand(), 1.0f), "and ignores writes");
		}

		void events(AnimCheck& check)
		{
			LuaAnimHarness h;
			h.run("count = 0; seen = ''; a = Animation.animator(find_entity('hero'))");
			h.run("a:on('footstep', function(self, event) count = count + 1; seen = event.name .. ':' .. event.clip .. ':' .. tostring(event.time) end)");
			h.run("a:play('Run')");
			h.frame(0.6f); // the event (0.5) fires in the module, the scripts hear it next frame
			check(h.run("return count") == "0", "events reach scripts one frame late");
			h.frame(0.01f);
			check(h.run("return count") == "1" && h.run("return seen") == "footstep:Run:0.5", "the handler gets name, clip and time");
			h.frame(0.01f);
			check(h.run("return count") == "1", "and hears an event once");

			h.run("a:off('footstep')");
			h.run("a:play('Run')");
			h.frame(0.6f);
			h.frame(0.01f);
			check(h.run("return count") == "1", "off removes the handler");

			// a handler that raises an error is logged and removed
			h.run("bad = 0; a:on('footstep', function() bad = bad + 1; error('boom') end)");
			h.run("a:play('Run')");
			h.frame(0.6f);
			h.frame(0.01f);
			h.run("a:play('Run')");
			h.frame(0.6f);
			h.frame(0.01f);
			check(h.run("return bad") == "1", "a handler that fails is removed after the first failure");

			// handlers go with the script instance that registered them
			h.run("gone = 0; a:on('footstep', function() gone = gone + 1 end)");
			h.Instances.ui_handlers().remove_owner(h.Owner);
			h.Instances.animation_handlers().remove_owner(h.Owner);
			h.run("a:play('Run')");
			h.frame(0.6f);
			h.frame(0.01f);
			check(h.run("return gone") == "0", "handlers end with their owner");
		}

		void graph_parameters(AnimCheck& check)
		{
			LuaAnimHarness h;
			h.use_graph();
			h.run("a = Animation.animator(find_entity('hero'))");
			h.frame(0.1f);
			check(h.run("return a:state()") == "Idle", "lua_graph_set_get_trigger_state: the graph starts in its default state");
			std::string error;
			h.run("a:set('speed', 2.5)", &error);
			check(error.empty() && h.run("return a:get('speed')") == "2.5", "lua_graph_set_get_trigger_state: set and get a float: " + error);
			h.run("a:set('go', true)", &error);
			check(error.empty() && h.run("return tostring(a:get('go'))") == "true", "lua_graph_set_get_trigger_state: a bool parameter reads back as a boolean");
			h.run("a:set('ghost', 1); a:set('speed', 'fast'); a:set('jump', 1); a:trigger('speed'); a:trigger('ghost')", &error);
			check(error.empty() && h.run("return a:get('speed')") == "2.5", "lua_graph_set_get_trigger_state: unknown names, wrong kinds and strings are ignored without an error: " + error);
			check(h.run("return tostring(a:get('ghost'))") == "nil", "lua_graph_set_get_trigger_state: an unknown parameter reads nil");
			h.run("a:trigger('jump')");
			h.frame(0.1f);
			check(h.run("return a:state()") == "Run" && h.run("return a:state('Base')") == "Run" && h.run("return a:state(1)") == "Run", "lua_graph_set_get_trigger_state: a trigger takes the transition (by default, by name, by number)");
			check(h.run("return tostring(a:state('Nope'))") == "nil", "lua_graph_set_get_trigger_state: an unknown layer reads nil");
		}

		void invalidation(AnimCheck& check)
		{
			LuaAnimHarness h;
			h.run("a = Animation.animator(find_entity('hero')); local m = a:joint_world('hand')");
			h.frame(0.1f);
			check(h.run("return a:joint_world('hand') ~= nil") == "true", "joint_world gives a matrix");
			h.run("destroy_entity(find_entity('hero'))");
			h.frame(0.1f);
			check(h.run("return tostring(a.ready)") == "nil" && h.run("return tostring(a.time)") == "nil" && h.run("return tostring(a:joint_world('hand'))") == "nil", "a handle to a destroyed entity reads nil");
			std::string error;
			h.run("a:play('Run'); a:stop(1); a.speed = 3", &error);
			check(error.empty(), "and ignores writes: " + error);
		}
	}

	void run_lua_checks(AnimCheck& check)
	{
		graph_parameters(check);
		animator_handles(check);
		playback(check);
		layers(check);
		events(check);
		invalidation(check);
	}
}
