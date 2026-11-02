#pragma once

#include <chrono>
#include <string>

#include <core/common.h>
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
			HotCreate,
			HotFirst,
			HotSecond,
			Done,
		};

		bool wait(u32 frames, const char* what, std::string& failure);
		std::string finish_checks();

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
	};
}
