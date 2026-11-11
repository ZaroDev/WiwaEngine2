#pragma once

#include <chrono>
#include <set>
#include <string>

#include <core/common.h>
#include <input/scripted_input_backend.h>
#include <scene/scene.h>
#include <scene/scene_editor.h>

#include "play_mode.h"

namespace wiwa
{
	// wiwa_editor --demo-smoke: drives the real editor through the demo scene, one step per frame. It opens assets/scenes/demo.wscene, plays it
	// and checks that the demo scripts moved things and logged no error; then it writes a script of its own into assets/scripts/, adds it to
	// an entity of the running scene, rewrites the file, and checks that the running instance runs the new code (file watcher, import,
	// reload, in-place swap: the whole way). The script files it made are deleted at the end.
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
			Physics,
			HotCreate,
			HotFirst,
			HotSecond,
			Done,
		};

		bool wait(u32 frames, const char* what, std::string& failure);
		void sample_fox(Scene& play);
		std::string check_fox() const;
		void sample_audio(Scene& play);
		std::string check_audio() const;
		std::string finish_checks();
		bool sample_physics(Scene& play, std::string& failure);

		// The HUD of the demo scene while it plays (the Ui phase), one step per frame.
		enum class UiResult
		{
			Running,
			Passed,
			Failed,
		};
		UiResult run_ui_step(std::string& failure);
		void restore_ui();

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
		Phase m_Phase = Phase::Open;
		u32 m_PhaseFrames = 0;
		std::chrono::steady_clock::time_point m_PhaseStart{};
		u32 m_RunFrames = 0;
		bool m_Started = false;
		std::uintmax_t m_LogStart = 0;

		// what the run phase watches: the lowest and highest value seen, per quantity
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

		// the Physics phase: the joints demo (door, cart, pendulum, bridge, breaking box) and the character demo (a patrolling character, a sliding
		// platform) of the scene while it plays
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
