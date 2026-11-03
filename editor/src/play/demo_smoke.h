#pragma once

#include <chrono>
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
			HotCreate,
			HotFirst,
			HotSecond,
			Done,
		};

		bool wait(u32 frames, const char* what, std::string& failure);
		std::string finish_checks();

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
