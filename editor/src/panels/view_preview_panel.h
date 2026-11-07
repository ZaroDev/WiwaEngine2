#pragma once
#include "panel.h"

#include <assets/asset_guid.h>
#include <renderer/view/render_view.h>
#include <scene/preview_scene.h>

namespace wiwa
{
	class AssetDatabase;
}

// The "View Preview" panel: a prefab in its own preview scene, drawn by an offscreen RenderView (views.md). It is the engine's view system in
// use inside the editor: orbit with the left mouse button, zoom with the wheel, pick the profile and a transparent background, and save what
// the view shows to a PNG through the readback path. The view only exists while the panel is visible. Main thread, between the ImGui frame's
// begin and end.
class ViewPreviewPanel : public Panel
{
public:
	explicit ViewPreviewPanel(const wiwa::AssetDatabase& database);
	~ViewPreviewPanel() override;

	virtual void on_draw() override;

private:
	void make_view(uint32_t width, uint32_t height);
	void save_png();

	const wiwa::AssetDatabase& m_Database;
	Ref<wiwa::Scene> m_Scene{};
	Scope<wiwa::PreviewModel> m_Model{};
	wiwa::RenderView m_View{};
	wiwa::AssetRef m_Prefab{};

	int m_Profile = 1; // ViewProfile
	bool m_Transparent = false;
	float m_Yaw = 20.0f;
	float m_Pitch = 15.0f;
	float m_Distance = 5.5f;
	float m_TargetHeight = 1.0f;
	wiwa::ViewProfile m_BuiltProfile = wiwa::ViewProfile::Preview;
	bool m_BuiltTransparent = false;
	std::string m_Status{};
};
