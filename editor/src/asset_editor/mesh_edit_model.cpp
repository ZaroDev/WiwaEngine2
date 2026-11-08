#include <editor_common.h>
#include "mesh_edit_model.h"

#include <unordered_map>

namespace wiwa
{
	MeshEditModel::MeshEditModel(MeshData mesh)
		: m_Mesh(std::move(mesh)), m_Skin(std::make_shared<const std::vector<MeshSkinVertex>>(m_Mesh.SkinVertices))
	{
	}

	MeshEditModel::State MeshEditModel::capture() const
	{
		State state;
		for (const MeshPrimitive& primitive : m_Mesh.Primitives)
		{
			state.Materials.push_back(primitive.Material);
		}
		state.Skeleton = m_Mesh.Skeleton;
		state.JointNames = m_Mesh.SkinJointNames;
		state.Skin = m_Skin;
		return state;
	}

	void MeshEditModel::apply(const State& state)
	{
		for (size_t i = 0; i < m_Mesh.Primitives.size() && i < state.Materials.size(); i++)
		{
			m_Mesh.Primitives[i].Material = state.Materials[i];
		}
		m_Mesh.Skeleton = state.Skeleton;
		m_Mesh.SkinJointNames = state.JointNames;
		if (state.Skin != m_Skin)
		{
			m_Skin = state.Skin;
			m_Mesh.SkinVertices = *m_Skin;
		}
	}

	bool MeshEditModel::set_material(size_t primitive, const AssetRef& material)
	{
		if (primitive >= m_Mesh.Primitives.size())
		{
			return false;
		}
		if (m_Mesh.Primitives[primitive].Material == material)
		{
			return true;
		}
		State before = capture();
		m_Mesh.Primitives[primitive].Material = material;
		m_History.push(std::move(before));
		return true;
	}

	bool MeshEditModel::can_rebind(const SkeletonData& target, std::vector<std::string>& missing) const
	{
		missing.clear();
		if (m_Mesh.SkinVertices.empty() || m_Mesh.SkinJointNames.empty())
		{
			return false;
		}
		std::unordered_map<std::string, size_t> targetIndex;
		for (size_t i = 0; i < target.JointNames.size(); i++)
		{
			targetIndex.emplace(target.JointNames[i], i);
		}
		for (const std::string& name : m_Mesh.SkinJointNames)
		{
			if (!targetIndex.contains(name))
			{
				missing.push_back(name);
			}
		}
		return missing.empty();
	}

	bool MeshEditModel::rebind(const AssetRef& skeleton, const SkeletonData& target, std::vector<std::string>& missing)
	{
		if (!can_rebind(target, missing))
		{
			return false;
		}
		if (m_Mesh.Skeleton == skeleton && m_Mesh.SkinJointNames == target.JointNames)
		{
			return true;
		}

		std::unordered_map<std::string, u32> targetIndex;
		for (size_t i = 0; i < target.JointNames.size(); i++)
		{
			targetIndex.emplace(target.JointNames[i], static_cast<u32>(i));
		}
		std::vector<u32> remap;
		for (const std::string& name : m_Mesh.SkinJointNames)
		{
			remap.push_back(targetIndex.at(name));
		}

		State before = capture();
		auto skin = std::make_shared<std::vector<MeshSkinVertex>>(m_Mesh.SkinVertices);
		for (MeshSkinVertex& vertex : *skin)
		{
			for (int k = 0; k < 4; k++)
			{
				vertex.Joints[k] = vertex.Joints[k] < remap.size() ? remap[vertex.Joints[k]] : 0u;
			}
		}
		m_Skin = std::move(skin);
		m_Mesh.SkinVertices = *m_Skin;
		m_Mesh.Skeleton = skeleton;
		m_Mesh.SkinJointNames = target.JointNames;
		m_History.push(std::move(before));
		return true;
	}

	void MeshEditModel::undo()
	{
		if (m_History.can_undo())
		{
			apply(m_History.undo(capture()));
		}
	}

	void MeshEditModel::redo()
	{
		if (m_History.can_redo())
		{
			apply(m_History.redo(capture()));
		}
	}
}
