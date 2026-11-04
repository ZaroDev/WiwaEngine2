#include <editor_common.h>
#include "demo_smoke.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <input/input_module.h>
#include <scene/reflection/component_registry.h>
#include <scripting/script_module.h>
#include <scripting/script_names.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_module.h>

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

		// the joints demo is watched from the start of the play to the end of the Input phase
		if (m_Phase >= Phase::Run && m_Phase <= Phase::Joints && m_Play.play_scene() != nullptr && !sample_joints(*m_Play.play_scene(), failure))
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

			if (++m_RunFrames < 150)
			{
				return false;
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

		case Phase::Joints:
		{
			// what sample_joints watched since the play started (the motors turn around every 2.5 to 3 seconds, so the run has seen whole swings)
			const JointsWatch& watch = m_Joints;
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
			if (!wait(2400, "the HUD checks", failure))
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
				m_Phase = Phase::Joints;
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
				m_UiStep = UiStep::Finish;
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
	bool DemoSmoke::sample_joints(Scene& play, std::string& failure)
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
		const Transform* links[5];
		bool linksFound = true;
		for (int i = 0; i < 5; i++)
		{
			links[i] = transform_of("Bridge link " + std::to_string(i));
			linksFound = linksFound && links[i] != nullptr;
		}
		if (door == nullptr || cart == nullptr || bob == nullptr || weak == nullptr || !linksFound)
		{
			failure = std::string("the joints demo entities are missing in the running scene (door ") + (door ? "yes" : "NO") + ", cart " + (cart ? "yes" : "NO") +
				", bob " + (bob ? "yes" : "NO") + ", weak box " + (weak ? "yes" : "NO") + ", bridge " + (linksFound ? "yes" : "NO") + ")";
			return false;
		}

		JointsWatch& watch = m_Joints;
		watch.DoorSwing = std::max(watch.DoorSwing, std::abs(door->Rotation.y));
		watch.CartLow = std::min(watch.CartLow, cart->Position.x);
		watch.CartHigh = std::max(watch.CartHigh, cart->Position.x);
		watch.BobPath += watch.BobSeen ? glm::length(bob->Position - watch.BobLast) : 0.0f;
		watch.BobLast = bob->Position;
		watch.BobSeen = true;
		watch.WeakLow = std::min(watch.WeakLow, weak->Position.y);
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
