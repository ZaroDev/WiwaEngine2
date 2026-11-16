#include <editor_common.h>
#include "mesh_editor.h"

#include <imgui.h>

#include <assets/asset_manager.h>
#include <assets/native_anim_files.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>
#include <core/file_io.h>
#include <asset_pipeline/file_utils.h>
#include <panels/asset_field.h>

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		ImVec4 error_color() { return ImVec4(1.0f, 0.45f, 0.4f, 1.0f); }

		std::optional<AssetRef> asset_at(const std::string& path)
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			return assets != nullptr ? assets->find_asset(path) : std::nullopt;
		}
	}

	bool MeshEditor::load(const std::filesystem::path& file, std::string& error)
	{
		const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read " + file.string();
			return false;
		}
		std::optional<MeshData> mesh = read_mesh_file(*bytes, error);
		if (!mesh)
		{
			return false;
		}
		m_File = file;
		m_Model = std::make_unique<MeshEditModel>(std::move(*mesh));
		m_Ref = asset_at(file.string()).value_or(AssetRef{});
		m_Message.clear();
		m_Missing.clear();
		return true;
	}

	AssetRef MeshEditor::preview_skeleton() const
	{
		if (!m_Model || !m_Model->data().Skeleton.valid())
		{
			return {};
		}
		return m_Host.Database.redirected(m_Model->data().Skeleton);
	}

	bool MeshEditor::save(std::string& error)
	{
		error.clear();
		if (!m_Model)
		{
			error = "there is nothing open";
			return false;
		}
		const std::vector<std::byte> bytes = write_mesh_file(m_Model->data());
		if (!write_file_atomic(m_File, bytes.data(), bytes.size(), error))
		{
			return false;
		}
		m_Model->mark_saved();
		return true;
	}

	void MeshEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		const MeshData& mesh = m_Model->data();
		ImGui::Text("%zu vertices, %zu triangles, %zu slots", mesh.Vertices.size(), mesh.Indices.size() / 3, mesh.Primitives.size());
		ImGui::TextDisabled("Bounds %.2f %.2f %.2f  to  %.2f %.2f %.2f", mesh.BoundsMin.x, mesh.BoundsMin.y, mesh.BoundsMin.z, mesh.BoundsMax.x, mesh.BoundsMax.y, mesh.BoundsMax.z);
		ImGui::TextDisabled("%zu joints in its skin", mesh.SkinJointNames.size());
		if (!m_Message.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Message.c_str());
		}

		AssetFieldContext context;
		context.Database = &m_Host.Database;

		if (ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen))
		{
			context.Expected = AssetType::Skeleton;
			std::string dropped;
			const AssetFieldEvent event = draw_asset_slot("##meshskeleton", "Skeleton", mesh.Skeleton, context, dropped);
			if (event == AssetFieldEvent::Dropped)
			{
				m_Message.clear();
				m_Missing.clear();
				bool readable = false;
				const std::optional<std::vector<std::byte>> text = read_binary_file(dropped);
				std::string reason;
				std::optional<SkeletonData> target;
				if (text && std::filesystem::path(dropped).extension() == ".wskel")
				{
					target = read_skeleton_file(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), reason);
					readable = target.has_value();
				}
				const std::optional<AssetRef> ref = asset_at(dropped);
				if (!readable || !ref)
				{
					m_Message = "That is not a skeleton file this editor can read" + (reason.empty() ? std::string{} : ": " + reason);
				}
				else if (!m_Model->rebind(*ref, *target, m_Missing))
				{
					m_Message = "That skeleton lacks joints this mesh is skinned to";
				}
			}
			else if (event == AssetFieldEvent::Cleared)
			{
				m_Message = "A skinned mesh needs a skeleton: drop another one to rebind";
			}
			if (!m_Missing.empty())
			{
				ImGui::TextColored(error_color(), "Missing joints:");
				for (const std::string& name : m_Missing)
				{
					ImGui::BulletText("%s", name.c_str());
				}
			}
		}

		if (ImGui::CollapsingHeader("Material slots", ImGuiTreeNodeFlags_DefaultOpen))
		{
			context.Expected = AssetType::Material;
			for (size_t i = 0; i < mesh.Primitives.size(); i++)
			{
				const std::string id = "##slot" + std::to_string(i);
				const std::string label = "Slot " + std::to_string(i) + " (" + std::to_string(mesh.Primitives[i].IndexCount / 3) + " tris)";
				std::string dropped;
				const AssetFieldEvent event = draw_asset_slot(id.c_str(), label.c_str(), mesh.Primitives[i].Material, context, dropped);
				if (event == AssetFieldEvent::Dropped)
				{
					if (const std::optional<AssetRef> ref = asset_at(dropped))
					{
						m_Model->set_material(i, *ref);
					}
				}
				else if (event == AssetFieldEvent::Cleared)
				{
					m_Model->set_material(i, AssetRef{});
				}
			}
		}
	}
}
