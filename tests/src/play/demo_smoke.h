#pragma once

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <core/common.h>
#include <input/scripted_input_backend.h>
#include <scene/scene.h>
#include <scene/scene_editor.h>

#include <play/play_mode.h>

namespace wiwa
{
	// wiwa_tests --demo-smoke: drives the real editor through the demo, one step per frame. The demo is a hub (assets/scenes/demo.wscene) and a scene
	// per group of features (scripts/gen_demo_scene.py, DEMO_SCENES): it plays the hub and clicks the first button of its menu (the lighting scene must
	// replace it), then opens and plays each feature scene in turn and checks that its demo scripts moved things and its HUD script ran. In the last one
	// (scripts, input and UI) it drives the HUD and the input demo, writes a script of its own into assets/scripts/, adds it to an entity of the running
	// scene, rewrites the file, and checks that the running instance runs the new code (file watcher, import, reload, in-place swap: the whole way).
	// Nothing may be logged as an error along the way. The script files it made are deleted at the end.
	class DemoSmoke
	{
	public:
		DemoSmoke(SceneEditor& editor, PlayMode& play);
		~DemoSmoke();

		// Once per frame (the editor's update). True when it is over; failure is empty if everything held.
		bool update(std::string& failure);

	private:
		enum class Phase
		{
			Open,
			Play,
			Run,
			Ui,
			Input,
			HotCreate,
			HotFirst,
			HotSecond,
			Done,
		};

		bool wait(u32 frames, const char* what, std::string& failure);
		void next_segment();
		void sample(Scene& play);
		std::string check_segment(Scene& play);
		std::string check_hud_status(Scene& play) const;
		std::string check_lighting() const;
		std::string check_features() const;
		std::string check_physics() const;
		std::string check_car() const;
		void sample_features(Scene& play);
		void sample_fox(Scene& play);
		std::string check_fox() const;
		void sample_audio(Scene& play);
		std::string check_audio() const;
		void sample_ai(Scene& play);
		std::string check_ai() const;
		bool ai_done() const;
		void sample_nav(Scene& play);
		std::string check_nav() const;
		std::string check_static_and_lods(Scene& scene, AssetManager* assets);
		bool nav_arrived() const;
		std::string finish_checks();
		void sample_physics(Scene& play);
		void sample_car(Scene& play);

		// The HUD of the demo scene while it plays (the Ui phase), one step per frame.
		enum class UiResult
		{
			Running,
			Passed,
			Failed,
		};
		UiResult run_ui_step(std::string& failure);
		void restore_ui();

		// The hub: its menu is drawn and its first button loads the lighting scene in place of the hub (load_scene from hub_menu.lua).
		enum class HubStep
		{
			Setup,
			WaitDrawn,
			WaitLoaded,
		};
		UiResult run_hub_step(std::string& failure);

		// A feature scene whose checks held: every button of its HUD's Controls panel is clicked once (its handler must run without an error).
		UiResult run_controls_step(Scene& play, std::string& failure);
		bool m_ClickingControls = false;
		std::vector<UiId> m_Controls{};
		size_t m_ControlIndex = 0;
		u32 m_ControlFrames = 0;

		// The Input phase: the input demo of the scene (a ball that moves, jumps and rebinds Jump) driven by a scripted input backend.
		enum class InputStep
		{
			Setup,
			WaitReady,
			Move,
			Jump,
			RebindClick,
			Heard,
			OldKey,
			NewKey,
			Saved,
		};
		UiResult run_input_step(std::string& failure);
		void restore_input();

		SceneEditor& m_Editor;
		PlayMode& m_Play;
		size_t m_Segment = 0; // the scene of the demo being checked (SEGMENTS in demo_smoke.cpp)
		std::chrono::steady_clock::time_point m_RunStart{};
		HubStep m_HubStep = HubStep::Setup;
		u32 m_HubFrames = 0;
		Phase m_Phase = Phase::Open;
		u32 m_PhaseFrames = 0;
		std::chrono::steady_clock::time_point m_PhaseStart{};
		u32 m_RunFrames = 0;
		bool m_Started = false;
		std::uintmax_t m_LogStart = 0;

		// what the run phase watches: the lowest and highest value seen, per quantity
		bool m_RingSeen = false;
		float m_RingBefore = 0.0f, m_RingAfter = 0.0f;
		float m_BallLow = 1e9f, m_BallHigh = -1e9f;
		float m_CameraLow = 1e9f, m_CameraHigh = -1e9f;
		float m_LightLow = 1e9f, m_LightHigh = -1e9f;

		// the animated warrior: its head socket moves, its palette leaves the rest pose, a crossfade happens, its footsteps are heard
		struct FoxWatch
		{
			glm::vec3 SocketLow{ 1e9f }, SocketHigh{ -1e9f };
			bool PaletteMoved = false;
			bool Crossfaded = false;
			bool GraphSeen = false;                // the Animator played its animation graph
			std::set<std::string> BaseStates{};    // the states the base layer of the graph was in
			std::set<std::string> UpperStates{};   // and the upper body layer
			bool HasAnimator = false;
			u64 EventsBefore = 0;
			u64 EventsSeen = 0;
			// decals (decals.md): the ones the scene places, the most footprints the warrior's footsteps made at once, and whether the meshes of the warrior opted out
			u32 PlacedDecals = 0;
			u32 MostFootprints = 0;
			u32 WarriorMeshes = 0;
			u32 WarriorMeshesReceiving = 0;
			// prefabs in prefabs (scene.md): the lamp heads of the lamp pair with the override of the scene (bright), and with the one of the pair prefab (blue)
			u32 NestedBright = 0;
			u32 NestedBlue = 0;
			// particle effects (vfx.md): the playing systems the scene places, the most dust puffs and impact sparks alive at once
			u32 PlayingEffects = 0;
			u32 MostDust = 0;
			u32 MostSparks = 0;
		};
		FoxWatch m_Fox{};

		// the audio demo: an emitter with an event on start and a script that posts, heard through the project's backend (the silent Null one records the calls)
		struct AudioWatch
		{
			bool HasEmitter = false;        // the entity has its AudioEmitter
			bool Registered = false;        // the audio module gave it an emitter of the backend
			bool LoopPosted = false;        // its PlayOnStart event reached the backend (Null backend)
			bool ScriptPosted = false;      // the script's Audio.post reached it (Null backend)
			bool Playing = false;           // something played (Wwise with banks)
			bool WwiseWithBanks = false;   // a backend that plays sound is selected (Wwise with banks, or MiniAudio)
			bool NullBackend = true;
		};
		AudioWatch m_Audio{};

		// the navigation demo (navigation.md): three agents walk round a wall to goals on the far side with nav_patrol.lua, over the navmesh the editor baked from
		// the scene (--nav-bake). The goals mirror NAV_GOALS in scripts/gen_demo_scene.py.
		struct NavWatch
		{
			bool HasMesh = false;              // the navigation module has the scene's navmesh
			u32 Agents = 0;                    // the "Nav agent N" entities found, with a NavAgent
			float Closest[3] = { 1e9f, 1e9f, 1e9f }; // how near each agent came to its goal
		};
		NavWatch m_Nav{};

		// the behaviour tree demo (behavior-trees.md): a guard patrols, sees the thief across the arena and chases it, and hears the noise maker. The names and the
		// thief's place mirror add_ai in scripts/gen_demo_scene.py.
		struct AiWatch
		{
			bool HasController = false;   // the AIModule made an instance for the guard
			bool Saw = false;             // can_see_target was true with the thief in seen_target
			bool Heard = false;           // heard_kind held "crash"
			float Closest = 1e9f;         // how near the guard came to the thief
		};
		AiWatch m_Ai{};
		std::chrono::steady_clock::time_point m_NavDeadline{};

		// the physics scene: the joints demo (door, cart, pendulum, bridge, breaking box) and the character demo (a patrolling character, a sliding
		// platform); and the vehicles scene: the autopilot car
		struct PhysicsWatch
		{
			float DoorSwing = 0.0f;                  // the largest |rotation.y| of the door
			float CartLow = 1e9f, CartHigh = -1e9f;  // x of the cart on its rail
			float BobPath = 0.0f;                    // how far the spring pendulum has travelled
			glm::vec3 BobLast{ 0.0f };
			bool BobSeen = false;
			float WeakLow = 1e9f;                    // the lowest y of the hanging box
			float BridgeStretch = 0.0f;              // the longest distance between two neighbouring planks
			float BridgeLow = 1e9f;                  // the lowest y of a plank
			float PatrolLowX = 1e9f, PatrolHighX = -1e9f; // the character that walks its lane on its own
			float PatrolLowY = 1e9f, PatrolHighY = -1e9f;
			float PlatformLow = 1e9f, PlatformHigh = -1e9f; // x of the platform that slides
			bool JointsSeen = false;                 // the physics scene's entities were found
			glm::vec3 CarStart{ 0.0f };              // the autopilot car: where it started and which way it faced
			glm::vec3 CarStartForward{ 0.0f, 0.0f, -1.0f };
			bool CarSeen = false;
			float CarFarthest = 0.0f;                // the farthest it got from the start
			float CarTurn = 0.0f;                    // the largest angle (degrees) between its heading and the first one
			bool CarFast = false;                    // it drove faster than 3 m/s
			bool CarStopped = false;                 // and afterwards came to a stop (below 0.5 m/s)
		};
		PhysicsWatch m_Watch{};

		EntityGuid m_Probe{};
		AssetRef m_HotScript{};

		// the Ui phase
		enum class UiStep
		{
			Setup,
			WaitDrawn,
			MoveBeacon,
			ClickOff,
			ClickOn,
			Slide,
			Release,
			IntroClick,    // the Play intro button shows the full screen Video widget and plays it (video.md)
			IntroPlaying,  // the clip plays, its picture is in the draw list; the Skip button is clicked
			IntroSkipped,  // the video stopped and its widget hid itself; the intro is played again
			IntroReplay,   // it plays again; the clip is moved to near its end
			IntroEnded,    // it ends on its own and the script hides the widget
			Finish,
		};
		UiStep m_UiStep = UiStep::Setup;
		u32 m_UiStepFrames = 0;
		bool m_UiTouched = false; // the canvas size and the input source were changed, and have to be put back
		class IUiInputSource* m_UiPreviousInput = nullptr;
		glm::vec2 m_ButtonCenter{ 0.0f };
		glm::vec2 m_SliderPoint{ 0.0f }; // where the middle of the slider's track is
		float m_BeaconX = 0.0f;
		float m_LightLevel = 0.0f;

		// the Input phase
		InputStep m_InputStep = InputStep::Setup;
		u32 m_InputStepFrames = 0;
		bool m_InputTouched = false; // the input module reads our backend and the bindings were reset: both are put back
		ScriptedInputBackend m_InputBackend{};
		glm::vec3 m_InputStart{ 0.0f };
		glm::vec2 m_RebindCenter{ 0.0f };
	};
}
