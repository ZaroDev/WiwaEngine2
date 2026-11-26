#include <editor_common.h>
#include "editor_smoke_driver.h"

#include <fstream>
#include <sstream>

#include <editor_module.h>
#include <imgui.h>
#include <ai/bt_edit_model.h>
#include <ai/bt_editor.h>
#include <animation/animation_panel.h>
#include <asset_editor/asset_editor_panel.h>
#include <asset_editor/vehicle_editor.h>
#include <asset_editor/vehicle_editor_checks.h>
#include <asset_pipeline/vehicle_file.h>
#include <asset_pipeline/asset_pipeline_module.h>
#include <asset_pipeline/physics_settings_file.h>
#include <input/input_edit_model.h>
#include <panels/audio_panel.h>
#include <panels/input_panel.h>
#include <panels/navigation_panel.h>
#include <panels/physics_panel.h>
#include <ui/ui_editor_panel.h>

#include <ai/bt_file.h>
#include <audio/audio_module.h>
#include <core/engine.h>
#include <input/input_map_io.h>
#include <physics/physics_module.h>
#include <scene/components.h>
#include <scene/entity_guid.h>
#include <scene/scene_module.h>

#include "ui/ui_smoke_test.h"

EditorSmokeDriver::EditorSmokeDriver(const std::vector<std::string>& arguments)
{
	for (const std::string& argument : arguments)
	{
		m_PlaySmoke = m_PlaySmoke || argument == "--play-smoke";
		m_UiEditorSmoke = m_UiEditorSmoke || argument == "--ui-editor-smoke";
		m_PhysicsPanelSmoke = m_PhysicsPanelSmoke || argument == "--physics-panel-smoke";
		m_InputPanelSmoke = m_InputPanelSmoke || argument == "--input-panel-smoke";
		m_AnimPanelSmoke = m_AnimPanelSmoke || argument == "--anim-panel-smoke";
		m_VfxEditorSmokeRequested = m_VfxEditorSmokeRequested || argument == "--vfx-editor-smoke";
		m_AudioPanelSmoke = m_AudioPanelSmoke || argument == "--audio-panel-smoke";
		m_NavPanelSmoke = m_NavPanelSmoke || argument == "--nav-panel-smoke";
		m_AiPanelSmoke = m_AiPanelSmoke || argument == "--ai-panel-smoke";
		m_DemoSmokeRequested = m_DemoSmokeRequested || argument == "--demo-smoke";
	}
}

bool EditorSmokeDriver::any_requested() const
{
	return m_PlaySmoke || m_UiEditorSmoke || m_PhysicsPanelSmoke || m_InputPanelSmoke || m_AnimPanelSmoke || m_VfxEditorSmokeRequested || m_AudioPanelSmoke || m_NavPanelSmoke || m_AiPanelSmoke || m_DemoSmokeRequested;
}

// The panel, play and demo smokes, once per editor frame (they used to be in EditorModule::update). Each member of the editor they touch is bound to a
// reference under its old name, so the checks read as they did there.
void EditorSmokeDriver::update(EditorModule& editor)
{
	wiwa::u32& m_Frame = editor.m_Frame;
	auto& m_SceneEditor = editor.m_SceneEditor;
	auto& m_PlayMode = editor.m_PlayMode;
	auto& m_UiEditor = editor.m_UiEditor;
	auto& m_PhysicsPanel = editor.m_PhysicsPanel;
	auto& m_PhysicsPanelFile = editor.m_PhysicsPanelFile;
	auto& m_InputPanel = editor.m_InputPanel;
	auto& m_AnimationPanel = editor.m_AnimationPanel;
	auto& m_AudioPanel = editor.m_AudioPanel;
	auto& m_NavPanel = editor.m_NavPanel;
	auto& m_AssetEditor = editor.m_AssetEditor;
	auto& m_Thumbnails = editor.m_Thumbnails;
	// The panel open flags and the dock layout come from editor/imgui.ini (the user's own): a panel they closed would never be drawn, and the run would
	// write its state back. Once ImGui has read the file (frame 3), a smoke opens the panels it checks and the run stops saving the layout.
	if (m_Frame == 3)
	{
		ImGui::GetIO().IniFilename = nullptr;
		for (Panel* panel : std::initializer_list<Panel*>{ m_PhysicsPanel, m_InputPanel, m_AnimationPanel, m_AudioPanel, m_NavPanel })
		{
			if (panel != nullptr)
			{
				panel->set_open(true);
			}
		}
	}

	if (m_DemoSmokeRequested && !m_DemoSmokeQueued)
	{
		// the startup scan may still be cooking the scene the demo opens: wait for it, or the smoke plays the previous cook
		m_DemoSmokeQueued = true;
		editor.when_scan_finished([this, &editor]() { m_DemoSmoke = CreateScope<wiwa::DemoSmoke>(*editor.m_SceneEditor, *editor.m_PlayMode); });
	}

	if (m_UiEditorSmoke && m_UiEditor != nullptr)
	{
		if (m_Frame == 20)
		{
			m_UiEditorSmokeFile = wiwa::write_ui_editor_smoke_panel();
			m_UiEditor->open(m_UiEditorSmokeFile);
		}
		if (m_Frame == 150)
		{
			const bool passed = m_UiEditor->loaded() && m_UiEditor->previewing();
			WI_INFO("Editor", "UI editor smoke test {0}", passed ? "passed" : "FAILED: the panel did not load or is not previewing");
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_PhysicsPanelSmoke && m_PhysicsPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_PhysicsPanel->request_focus();
		}
		if (m_Frame == 40)
		{
			// a click in the matrix: layers 0 and 1 stop colliding (a layer is named first so that the matrix shows it)
			m_PhysicsPanel->section()->set_layer_name(1, "Smoke");
			m_PhysicsPanel->section()->set_collides(0, 1, false);
		}

		if (m_Frame == 130)
		{
			wiwa::PhysicsLayers saved;
			std::string error;
			const bool read = wiwa::load_physics_layers(m_PhysicsPanelFile, saved, error);
			wiwa::PhysicsModule* physics = wiwa::Engine::get().get_module<wiwa::PhysicsModule>();
			const bool passed = m_PhysicsPanel->frames_drawn() >= 100 && read && saved.Names[1] == "Smoke" && !saved.collides(0, 1) && saved.collides(0, 0) && saved.collides(1, 1) &&
				physics != nullptr && !physics->layers().collides(0, 1) && m_PhysicsPanel->section()->last_error().empty();
			WI_INFO("Editor", "Physics panel smoke test {0} (frames {1}, read {2}, layer 1 \"{3}\", 0-1 collide {4}, error \"{5}\")", passed ? "passed" : "FAILED", m_PhysicsPanel->frames_drawn(), read, saved.Names[1], saved.collides(0, 1), m_PhysicsPanel->section()->last_error());
			std::error_code removed;
			std::filesystem::remove(m_PhysicsPanelFile, removed);
			if (!passed)
			{
				wiwa::Engine::get().set_exit_code(1);
				wiwa::Engine::get().request_quit();
			}
		}

		// then the vehicle tools (the Asset Editor shares a dock with the Physics panel, so after it): the checks that need no frame, and a
		// .wvehicle in the Asset Editor for 120 frames with ImGui asserts on, edited through the model and saved
		if (m_Frame == 135)
		{
			m_VehicleFailure = wiwa::run_vehicle_editor_checks();
			m_VehiclePanelFile = std::filesystem::temp_directory_path() / "wiwa_physics_panel_smoke.wvehicle";
			std::ofstream(m_VehiclePanelFile, std::ios::binary) << wiwa::vehicle_to_json(wiwa::default_vehicle_settings()).dump(2);
			if (m_AssetEditor == nullptr)
			{
				m_VehicleFailure += " no Asset Editor panel";
			}
			else
			{
				m_AssetEditor->set_open(true);
				if (!m_AssetEditor->open_file(m_VehiclePanelFile))
				{
					m_VehicleFailure += " the .wvehicle did not open (" + m_AssetEditor->last_error() + ")";
				}
			}
		}
		if (m_Frame == 200 && m_AssetEditor != nullptr)
		{
			wiwa::VehicleEditor* vehicleEditor = dynamic_cast<wiwa::VehicleEditor*>(m_AssetEditor->editor());
			wiwa::VehicleSettingsData next = vehicleEditor != nullptr && vehicleEditor->model() != nullptr ? vehicleEditor->model()->data() : wiwa::VehicleSettingsData{};
			next.MaxBrakeTorque = 1234.0f;
			if (vehicleEditor == nullptr || vehicleEditor->model() == nullptr || !vehicleEditor->model()->edit(next))
			{
				m_VehicleFailure += " the vehicle editor did not take the edit";
			}
			else
			{
				m_AssetEditor->save(); // the panel's Save (Ctrl+S): it knows the change on disk is its own
				if (!m_AssetEditor->last_error().empty())
				{
					m_VehicleFailure += " the vehicle did not save (" + m_AssetEditor->last_error() + ")";
				}
			}
		}
		if (m_Frame == 260)
		{
			wiwa::VehicleEditor* vehicleEditor = m_AssetEditor != nullptr ? dynamic_cast<wiwa::VehicleEditor*>(m_AssetEditor->editor()) : nullptr;
			std::ifstream stream(m_VehiclePanelFile, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			stream.close();
			wiwa::VehicleSettingsData reloaded;
			std::string readError;
			const bool savedVehicle = wiwa::vehicle_from_text(text, reloaded, readError) && reloaded.MaxBrakeTorque == 1234.0f;
			const bool vehiclePassed = m_VehicleFailure.empty() && vehicleEditor != nullptr && m_AssetEditor->open_kind() == "Vehicle" && vehicleEditor->frames_drawn() >= 100
				&& savedVehicle && !vehicleEditor->dirty();
			WI_INFO("Editor", "Vehicle editor smoke {0} (kind '{1}', frames {2}, panel frames {5}, saved and reloaded {3}, {4})", vehiclePassed ? "passed" : "FAILED", m_AssetEditor != nullptr ? m_AssetEditor->open_kind() : "",
				vehicleEditor != nullptr ? vehicleEditor->frames_drawn() : 0u, savedVehicle, m_VehicleFailure, m_AssetEditor != nullptr ? m_AssetEditor->frames_with_editor() : 0u);
			std::error_code vehicleRemoved;
			std::filesystem::remove(m_VehiclePanelFile, vehicleRemoved);
			wiwa::Engine::get().set_exit_code(vehiclePassed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_AudioPanelSmoke && m_AudioPanel != nullptr)
	{
		wiwa::AudioModule* audio = wiwa::Engine::get().get_module<wiwa::AudioModule>();
		// The editor applies audio.wsettings (MiniAudio here) once the first import scan is done, and how late that is depends on the machine: select the Null
		// backend only after it did, and measure the rest of the run from there.
		if (audio != nullptr && m_AudioNullFrame == 0 && !editor.m_PendingAudio && m_Frame >= 5)
		{
			audio->select_backend(wiwa::AudioBackendKind::Null, wiwa::AudioInitInfo{}); // whatever the project chose: the smoke checks the panel, not a vendor
			m_AudioNullFrame = m_Frame;
		}
		if (m_Frame == 10)
		{
			m_AudioPanel->request_focus();
		}
		if (m_AudioNullFrame != 0 && m_Frame == m_AudioNullFrame + 35 && audio != nullptr && audio->active_kind() == wiwa::AudioBackendKind::Null)
		{
			// the panel auditions on the Null backend: it plays whatever event it is given when no bank metadata is loaded
			m_AudioPanelAuditioned = m_AudioPanel->audition("Play_PanelSmoke") && audio->backend().playing_count() == 1 && audio->auditioning();
		}
		if (m_AudioNullFrame != 0 && m_Frame == m_AudioNullFrame + 95 && audio != nullptr && m_AudioPanelAuditioned)
		{
			m_AudioPanel->stop_audition();
		}
		const wiwa::u32 giveUpFrame = 20000; // the scan never finished: report it as a failure instead of waiting for ever
		if ((m_AudioNullFrame != 0 && m_Frame == std::max<wiwa::u32>(130, m_AudioNullFrame + 125)) || m_Frame == giveUpFrame)
		{
			const bool silent = audio != nullptr && audio->backend().playing_count() == 0 && !audio->auditioning();
			const bool passed = m_AudioPanel->frames_drawn() >= 100 && audio != nullptr && audio->active_kind() == wiwa::AudioBackendKind::Null && m_AudioPanelAuditioned && silent;
			WI_INFO("Editor", "Audio panel smoke test {0} (frames {1}, auditioned {2}, silent after stop {3}, backend {4}, Null selected at frame {5})", passed ? "passed" : "FAILED", m_AudioPanel->frames_drawn(), m_AudioPanelAuditioned, silent, audio != nullptr ? wiwa::audio_backend_name(audio->active_kind()) : "none", m_AudioNullFrame);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_AiPanelSmoke && m_AssetEditor != nullptr)
	{
		if (m_Frame == 5)
		{
			m_AssetEditor->set_open(true); // the saved layout of an earlier run may have closed it
		}
		if (m_Frame == 10)
		{
			m_AiPanelFile = std::filesystem::temp_directory_path() / "wiwa_ai_panel_smoke.wbt";
			wiwa::BtEditModel seed{ wiwa::BehaviorTreeDoc{} };
			const std::string root = seed.add_node("Sequence", "");
			seed.add_node("Wait", root);
			std::string error;
			if (!seed.save(m_AiPanelFile))
			{
				m_AiPanelFailure = "cannot write the smoke tree: " + seed.error();
			}
			m_AssetEditor->open_file(m_AiPanelFile);
		}
		wiwa::BehaviorTreeEditor* editor = dynamic_cast<wiwa::BehaviorTreeEditor*>(m_AssetEditor->editor());
		if (m_Frame == 40 && editor != nullptr && editor->model() != nullptr)
		{
			// what the canvas menus and the tool pane do, through the model
			wiwa::BtEditModel& model = *editor->model();
			const std::string root = model.doc().Root;
			const std::string log = model.add_node("Log", root);
			model.set_param(log, "message", "hello");
			model.add_key("alert", wiwa::BtValueType::Bool);
			const std::string guard = model.add_node("Guard", root, 0);
			model.set_param(guard, "key", "alert");
			model.set_param(guard, "op", "true");
			model.add_node("Wait", guard);
			editor->select(log);
		}
		if (m_Frame == 70 && editor != nullptr && editor->model() != nullptr)
		{
			m_AssetEditor->save(); // the toolbar's Save: the panel then knows the file change is its own (a refusal shows as last_error)
		}
		if (m_Frame == 130)
		{
			std::string problems;
			bool saved = false;
			if (editor != nullptr && editor->model() != nullptr)
			{
				std::ifstream stream(m_AiPanelFile, std::ios::binary);
				std::stringstream text;
				text << stream.rdbuf();
				std::string error;
				const std::optional<wiwa::BehaviorTreeDoc> back = wiwa::read_bt_file(text.str(), error);
				saved = back && *back == editor->model()->doc() && !editor->dirty() && editor->model()->doc().Nodes.size() == 5;
			}
			const bool passed = m_AiPanelFailure.empty() && editor != nullptr && m_AssetEditor->open_kind() == "Behaviour tree" && m_AssetEditor->frames_with_editor() >= 100 && m_AssetEditor->last_error().empty() && saved && editor->frames_drawn() >= 100;
			WI_INFO("Editor", "AI panel smoke test {0} (editor frames {1}, tree editor frames {2}, kind '{3}', saved and reloaded equal {4}, error \"{5}\", {6})", passed ? "passed" : "FAILED", m_AssetEditor->frames_with_editor(), editor != nullptr ? editor->frames_drawn() : 0u, m_AssetEditor->open_kind(), saved, m_AssetEditor->last_error(), m_AiPanelFailure);
			std::error_code removed;
			std::filesystem::remove(m_AiPanelFile, removed);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_NavPanelSmoke && m_NavPanel != nullptr)
	{
		if (m_Frame == 5)
		{
			m_NavPanel->set_open(true); // the saved layout of an earlier run may have closed it
		}
		if (m_Frame == 10)
		{
			m_NavPanel->request_focus();
		}
		if (m_Frame == 20)
		{
			m_NavPanelVolume = m_NavPanel->create_volume();
		}
		if (m_Frame == 130)
		{
			const bool made = m_NavPanelVolume != entt::null && m_SceneEditor->scene().registry().valid(m_NavPanelVolume) && m_SceneEditor->scene().registry().all_of<wiwa::NavMeshVolume>(m_NavPanelVolume) && m_NavPanel->current_volume() == m_NavPanelVolume;
			const bool passed = m_NavPanel->frames_drawn() >= 100 && made;
			WI_INFO("Editor", "Nav panel smoke test {0} (frames {1}, volume made through the panel {2})", passed ? "passed" : "FAILED", m_NavPanel->frames_drawn(), made);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_InputPanelSmoke && m_InputPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_InputPanel->request_focus();
		}
		if (m_Frame == 100)
		{
			// open a map, add an action with a binding through the model, and save it
			m_InputPanelFile = std::filesystem::temp_directory_path() / "wiwa_input_panel_smoke.winput";
			{
				std::ofstream out(m_InputPanelFile, std::ios::binary | std::ios::trunc);
				out << R"({"version":1,"maps":[{"name":"Gameplay","actions":[{"name":"Jump","type":"Button","bindings":[{"control":"Key.Space"}]}]}]})";
			}
			if (m_InputPanel->open_path(m_InputPanelFile) && m_InputPanel->model() != nullptr)
			{
				wiwa::InputEditModel* model = m_InputPanel->model();
				wiwa::Binding binding;
				binding.Parts[0] = wiwa::control(wiwa::Key::F9);
				const size_t action = model->add_action(0, "Smoke", wiwa::ActionType::Button);
				model->add_binding(0, action, binding);
				m_InputPanel->save();
			}
		}
		if (m_Frame == 130)
		{
			std::ifstream in(m_InputPanelFile, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			wiwa::InputMapData saved;
			std::string error;
			const bool read = wiwa::input_map_from_text(text.str(), saved, error);
			const wiwa::Action* added = read && !saved.Maps.empty() && saved.Maps[0].Actions.size() == 2 ? &saved.Maps[0].Actions[1] : nullptr;
			const bool passed = m_InputPanel->frames_drawn() >= 100 && read && added != nullptr && added->Name == "Smoke" && added->Bindings.size() == 1 &&
				added->Bindings[0].Parts[0] == wiwa::control(wiwa::Key::F9) && m_InputPanel->last_error().empty() &&
				m_InputPanel->model() != nullptr && !m_InputPanel->model()->dirty();
			WI_INFO("Editor", "Input panel smoke test {0} (frames {1}, read {2}, actions {3}, error \"{4}\")", passed ? "passed" : "FAILED", m_InputPanel->frames_drawn(), read,
				read && !saved.Maps.empty() ? saved.Maps[0].Actions.size() : 0, m_InputPanel->last_error());
			std::error_code removed;
			std::filesystem::remove(m_InputPanelFile, removed);
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_AnimPanelSmoke && m_AnimationPanel != nullptr)
	{
		if (m_Frame == 10)
		{
			m_AnimationPanel->request_focus();
			// the Asset Editor opens the demo warrior's animation graph (canvas, tool pane and the preview that plays it)
			m_AssetEditor->open_file("assets/models/characters/Warrior_animation/warrior.wgraph");
		}
		if (m_Frame == 130)
		{
			// the panels were drawn for the whole run (the events are edited in the Asset Editor now); the graph plays in the Asset Editor's preview
			const bool animationPanel = m_AnimationPanel->frames_drawn() >= 100 && m_AnimationPanel->last_error().empty();
			const bool graphEditor = m_AssetEditor->open_kind() == "Animation graph" && m_AssetEditor->frames_with_editor() >= 100 && m_AssetEditor->last_error().empty()
				&& !m_AssetEditor->preview_state().empty();
			const bool passed = animationPanel && graphEditor;
			WI_INFO("Editor", "Animation panel smoke test {0} (frames {1}, error \"{2}\"; graph editor '{3}' drew {4} frames, error \"{5}\", preview state '{6}')", passed ? "passed" : "FAILED",
				m_AnimationPanel->frames_drawn(), m_AnimationPanel->last_error(), m_AssetEditor->open_kind(), m_AssetEditor->frames_with_editor(), m_AssetEditor->last_error(), m_AssetEditor->preview_state());
			wiwa::Engine::get().set_exit_code(passed ? 0 : 1);
			wiwa::Engine::get().request_quit();
		}
	}

	if (m_VfxEditorSmokeRequested && m_AssetEditor != nullptr)
	{
		if (m_VfxEditorSmoke == nullptr)
		{
			m_VfxEditorSmoke = CreateScope<wiwa::VfxEditorSmoke>(*m_AssetEditor, wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>()->get_database(), m_Thumbnails.get());
		}
		std::string failure;
		if (m_VfxEditorSmoke->update(static_cast<wiwa::u32>(m_Frame), failure))
		{
			WI_INFO("Editor", "{0}", failure.empty() ? std::string("VFX editor smoke test passed") : "VFX editor smoke test FAILED: " + failure);
			wiwa::Engine::get().set_exit_code(failure.empty() ? 0 : 1);
			wiwa::Engine::get().request_quit();
			m_AssetEditor->close_preview();
			m_VfxEditorSmoke.reset();
			m_VfxEditorSmokeRequested = false;
		}
	}
	if (m_PlaySmoke && m_Frame == 30)
	{
		const std::string failure = run_play_smoke(editor);
		if (!failure.empty())
		{
			WI_INFO("Editor", "Play smoke test FAILED: {0}", failure);
		}
		if (!failure.empty())
		{
			wiwa::Engine::get().set_exit_code(1);
			wiwa::Engine::get().request_quit();
		}
	}
	if (m_PlaySmoke && m_PlaySmokeFinalFrame != 0 && m_Frame == m_PlaySmokeFinalFrame)
	{
		wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
		std::string failure;
		if (scenes->get_active() != m_PlaySmokeEdit || m_SceneEditor->get_scene() != m_PlaySmokeEdit)
		{
			failure = "a load that outlived Stop replaced the edit scene";
		}
		else if (m_PlaySmokeEdit->registry().storage<wiwa::Id>().size() != m_PlaySmokeCount)
		{
			failure = "a load that outlived Stop added entities to the edit scene";
		}
		WI_INFO("Editor", "{0}", failure.empty() ? std::string("Play smoke test passed") : "Play smoke test FAILED: " + failure);
		wiwa::Engine::get().set_exit_code(failure.empty() ? 0 : 1);
		wiwa::Engine::get().request_quit();
	}

	if (m_DemoSmoke != nullptr && m_Frame >= 30)
	{
		std::string failure;
		if (m_DemoSmoke->update(failure))
		{
			WI_INFO("Editor", "{0}", failure.empty() ? std::string("Demo smoke test passed") : "Demo smoke test FAILED: " + failure);
			wiwa::Engine::get().set_exit_code(failure.empty() ? 0 : 1);
			wiwa::Engine::get().request_quit();
			m_DemoSmoke.reset();
			m_DemoSmokeDone = true;
		}
	}

}

std::string EditorSmokeDriver::run_play_smoke(EditorModule& editor)
{
	wiwa::u32& m_Frame = editor.m_Frame;
	auto& m_SceneEditor = editor.m_SceneEditor;
	auto& m_PlayMode = editor.m_PlayMode;
	wiwa::SceneModule* scenes = wiwa::Engine::get().get_module<wiwa::SceneModule>();
	Ref<wiwa::Scene> edit = m_SceneEditor->get_scene();

	const entt::entity probe = m_SceneEditor->create_entity(entt::null);
	const entt::entity other = m_SceneEditor->create_entity(entt::null);
	const wiwa::EntityGuid probeGuid = edit->registry().get<wiwa::Id>(probe).Guid;
	const wiwa::EntityGuid otherGuid = edit->registry().get<wiwa::Id>(other).Guid;
	const bool dirtyBefore = m_SceneEditor->dirty();
	const glm::vec3 positionBefore = edit->registry().get<wiwa::Transform>(probe).Position;

	m_SceneEditor->select(probe);
	m_PlaySmokeEdit = edit;
	m_PlaySmokeCount = edit->registry().storage<wiwa::Id>().size();

	m_PlayMode->play();
	if (m_PlayMode->state() != wiwa::PlayState::Playing)
	{
		return "play did not start";
	}
	if (m_SceneEditor->selected() == entt::null || m_PlayMode->play_scene()->registry().get<wiwa::Id>(m_SceneEditor->selected()).Guid != probeGuid)
	{
		return "the selection was lost when play started";
	}

	// a script asks for a scene to be added, and Stop comes before the load is done: it must not end up in the scene being edited
	scenes->load_async(std::string_view("assets/scenes/demo.wscene"), wiwa::LoadMode::Additive);

	Ref<wiwa::Scene> clone = m_PlayMode->play_scene();
	if (clone == nullptr || clone == edit || scenes->get_active() != clone || m_SceneEditor->get_scene() != clone || !m_SceneEditor->playing())
	{
		return "the clone is not the scene that is shown";
	}
	const entt::entity cloneProbe = clone->find(probeGuid);
	const entt::entity cloneOther = clone->find(otherGuid);
	if (cloneProbe == entt::null || cloneOther == entt::null)
	{
		return "the clone lacks the entities of the edit scene";
	}

	clone->registry().get<wiwa::Transform>(cloneProbe).Position = glm::vec3(9.0f);
	clone->destroy(cloneOther);

	m_PlayMode->pause();
	m_PlayMode->step();
	m_PlayMode->update();
	m_PlayMode->update();
	if (m_PlayMode->state() != wiwa::PlayState::Paused)
	{
		return "step left play mode";
	}
	m_PlayMode->resume();
	m_PlayMode->stop();

	if (m_PlayMode->state() != wiwa::PlayState::Edit || scenes->get_active() != edit || m_SceneEditor->get_scene() != edit || m_SceneEditor->playing())
	{
		return "the edit scene was not put back";
	}
	if (edit->registry().get<wiwa::Transform>(probe).Position != positionBefore || edit->find(otherGuid) == entt::null)
	{
		return "the edit scene was changed by playing";
	}
	if (m_SceneEditor->dirty() != dirtyBefore)
	{
		return "the dirty flag changed";
	}
	m_PlaySmokeFinalFrame = m_Frame + 90; // and later: nothing may have landed in the edit scene
	return {};
}

