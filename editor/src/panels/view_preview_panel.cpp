#include <editor_common.h>
#include "view_preview_panel.h"

#include <imgui.h>

#include <asset_pipeline/asset_database.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/view/view_camera.h>

#include "asset_field.h"
#include "png_writer.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_VIEW_PREVIEW_LOG(level, ...) WI_LOG(level, "ViewPreview", __VA_ARGS__)

namespace
{
	constexpr float FOV = 35.0f;
	const char* const PROFILE_NAMES[] = { "Full", "Preview", "Thumbnail" };
}

ViewPreviewPanel::ViewPreviewPanel(const wiwa::AssetDatabase& database)
	: Panel("View Preview", PanelCategory::Renderer), m_Database(database)
{
	m_Open = false; // opened from the Window menu
}

ViewPreviewPanel::~ViewPreviewPanel()
{
	// The handle goes before the scene it shows.
	m_View = wiwa::RenderView{};
	m_Model.reset();
	m_Scene.reset();
}

void ViewPreviewPanel::make_view(uint32_t width, uint32_t height)
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	wiwa::ViewDesc description;
	description.Width = width;
	description.Height = height;
	description.Profile = static_cast<wiwa::ViewProfile>(m_Profile);
	description.TransparentBackground = m_Transparent;
	description.Mode = wiwa::ViewMode::Continuous;
	m_View = render->create_view(description);
	if (!m_View.valid())
	{
		m_Status = "Could not make a view (too many are live)";
		return;
	}
	m_BuiltProfile = description.Profile;
	m_BuiltTransparent = m_Transparent;
	m_Status.clear();

	if (m_Scene == nullptr)
	{
		m_Scene = wiwa::make_preview_scene();
		m_Model = CreateScope<wiwa::PreviewModel>(*wiwa::Engine::get().get_module<wiwa::AssetManager>(), m_Scene);
		if (m_Prefab.valid())
		{
			m_Model->set_prefab(m_Prefab);
		}
	}
	m_View.set_scene(m_Scene);
}

void ViewPreviewPanel::save_png()
{
	if (!m_View.valid())
	{
		return;
	}
	m_Status = "Saving...";
	m_View.read_back([this](wiwa::ImageData image)
		{
			if (image.Rgba.empty())
			{
				m_Status = "The view could not be read back";
				return;
			}
			const std::filesystem::path path = std::filesystem::path("Library") / "view_preview.png";
			if (!png_writer::write_rgba(path, image.Width, image.Height, image.Rgba))
			{
				m_Status = "Could not write " + path.string();
				return;
			}
			// Corner and centre pixels: a transparent background shows as alpha 0 in the corner.
			auto pixel = [&](uint32_t x, uint32_t y)
			{
				const uint8_t* p = image.Rgba.data() + (static_cast<size_t>(y) * image.Width + x) * 4;
				return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + "," + std::to_string(p[3]);
			};
			WI_VIEW_PREVIEW_LOG(Info, "View preview saved to {0} ({1}x{2}): top-left rgba {3}, centre rgba {4}", path.string(), image.Width, image.Height, pixel(0, 0), pixel(image.Width / 2, image.Height / 2));
			m_Status = "Saved " + path.string();
		});
}

void ViewPreviewPanel::on_draw()
{
	ImGui::SetNextWindowSize(ImVec2(420.0f, 460.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(get_name(), &m_Open))
	{
		// Hidden: the view is not drawn any more (it costs nothing without one).
		m_View = wiwa::RenderView{};
		ImGui::End();
		return;
	}

	// Controls
	int profile = m_Profile;
	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::Combo("Profile", &profile, PROFILE_NAMES, 3))
	{
		m_Profile = profile;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Transparent", &m_Transparent);
	ImGui::SameLine();
	if (ImGui::Button("Save PNG"))
	{
		save_png();
	}

	AssetFieldContext context;
	context.Expected = wiwa::AssetType::Prefab;
	context.Database = &m_Database;
	std::string dropped;
	switch (draw_asset_slot("##viewpreviewprefab", "Prefab", m_Prefab, context, dropped))
	{
	case AssetFieldEvent::Dropped:
		if (wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>())
		{
			if (const std::optional<wiwa::AssetRef> found = assets->find_asset(dropped))
			{
				m_Prefab = *found;
			}
		}
		break;
	case AssetFieldEvent::Cleared:
		m_Prefab = wiwa::AssetRef{};
		break;
	default:
		break;
	}
	ImGui::SliderFloat("Look at height", &m_TargetHeight, -2.0f, 4.0f);

	if (m_Model != nullptr)
	{
		m_Model->set_prefab(m_Prefab);
		m_Model->update();
		if (m_Model->failed())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "The prefab failed to load");
		}
		else if (m_Prefab.valid() && !m_Model->ready())
		{
			ImGui::TextDisabled("Loading...");
		}
	}
	if (!m_Status.empty())
	{
		ImGui::TextDisabled("%s", m_Status.c_str());
	}

	// The image takes the rest of the window.
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 16.0f);
	size.y = std::max(size.y, 16.0f);
	const uint32_t width = static_cast<uint32_t>(size.x);
	const uint32_t height = static_cast<uint32_t>(size.y);

	// A view made for other settings is made again (the scene stays).
	if (m_View.valid() && (m_BuiltProfile != static_cast<wiwa::ViewProfile>(m_Profile) || m_BuiltTransparent != m_Transparent))
	{
		m_View = wiwa::RenderView{};
	}
	if (!m_View.valid())
	{
		make_view(width, height);
	}
	if (!m_View.valid())
	{
		ImGui::End();
		return;
	}

	m_View.resize(width, height);
	m_View.set_camera(wiwa::make_orbit_camera(glm::vec3(0.0f, m_TargetHeight, 0.0f), m_Distance, m_Yaw, m_Pitch, FOV, size.x / size.y));

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	if (m_Transparent)
	{
		// A checkerboard shows what is transparent.
		ImDrawList* list = ImGui::GetWindowDrawList();
		const float tile = 12.0f;
		for (float y = 0.0f; y < size.y; y += tile)
		{
			for (float x = 0.0f; x < size.x; x += tile)
			{
				const bool dark = (static_cast<int>(x / tile) + static_cast<int>(y / tile)) % 2 == 0;
				list->AddRectFilled(ImVec2(origin.x + x, origin.y + y), ImVec2(origin.x + std::min(x + tile, size.x), origin.y + std::min(y + tile, size.y)), dark ? IM_COL32(60, 60, 64, 255) : IM_COL32(90, 90, 96, 255));
			}
		}
	}
	ImGui::Image(static_cast<ImTextureID>(m_View.imgui_texture_id()), size);

	if (ImGui::IsItemHovered())
	{
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			m_Yaw -= delta.x * 0.4f;
			m_Pitch = std::clamp(m_Pitch + delta.y * 0.4f, -85.0f, 85.0f);
		}
		m_Distance = std::clamp(m_Distance * (1.0f - ImGui::GetIO().MouseWheel * 0.1f), 0.3f, 100.0f);
	}
	if (m_View.failed())
	{
		ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), IM_COL32(255, 100, 100, 255), "The view could not make its target");
	}

	ImGui::End();
}
