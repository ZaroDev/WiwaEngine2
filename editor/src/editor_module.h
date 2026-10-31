#pragma once

#include <core/module.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <panels/panels.h>
#include <play/play_mode.h>
#include <scene/scene_editor.h>

class EditorModule : public wiwa::IModule
{
public:
	virtual ~EditorModule() override = default;

	virtual void init() override;
	virtual void shutdown() override;

	virtual void pre_update(float deltaTime) override;
	virtual void update(float deltaTime) override;
	virtual void post_update(float deltaTime) override;

	virtual void pre_physics_update(float deltaTime) override;
	virtual void physics_update(float deltaTime) override;
	virtual void post_physics_update(float deltaTime) override;

	virtual void pre_render_update(float deltaTime) override;
	virtual void render_update(float deltaTime) override;
	virtual void post_render_update(float deltaTime) override;

	virtual wiwa::UpdateFlags get_update_flags() const { return wiwa::UpdateFlags::ALL; };
	virtual std::string get_name() const { return "Editor"; };

	virtual int get_priority() const { return 0; }

private:
	void dock_space();
	void main_bar();
	void play_toolbar();
	// --play-smoke: plays the scene, changes the copy, stops, and checks that the scene being edited came back untouched.
	std::string run_play_smoke();
	void set_theme();
private:
	std::vector<Ref<Panel>> m_Panels{};
	Scope<wiwa::SceneEditor> m_SceneEditor;
	Scope<wiwa::IPlayHost> m_PlayHost;
	Scope<wiwa::PlayMode> m_PlayMode;
	bool m_PlaySmoke = false;
	wiwa::u32 m_Frame = 0;
	wiwa::u32 m_PlaySmokeFinalFrame = 0;
	Ref<wiwa::Scene> m_PlaySmokeEdit{};
	size_t m_PlaySmokeCount = 0;
	int m_Operation = ImGuizmo::TRANSLATE;
	bool m_LocalSpace = false;
};