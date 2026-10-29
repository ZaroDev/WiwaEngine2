#pragma once

#include <core/module.h>

#include <panels/panels.h>
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
	void set_theme();
private:
	std::vector<Ref<Panel>> m_Panels{};
	Scope<wiwa::SceneEditor> m_SceneEditor;
};