#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <assets/asset_data.h>

#include <asset_pipeline/mesh_lods.h>

#include "edit_history.h"

namespace wiwa
{
	// The document the Mesh editor edits: a skinned mesh's material slots (the material of each primitive) and its skeleton binding, and the
	// history. No ImGui. The geometry of the full level never changes; levels of detail can be baked and cleared. A call that is refused changes nothing and makes no undo step.
	class MeshEditModel
	{
	public:
		explicit MeshEditModel(MeshData mesh);

		const MeshData& data() const { return m_Mesh; }

		bool set_material(size_t primitive, const AssetRef& material);

		// Whether the mesh can be driven by the target skeleton: every joint name its skin uses has to exist there. missing lists the names that do
		// not. Meshes that are not skinned cannot be rebound.
		bool can_rebind(const SkeletonData& target, std::vector<std::string>& missing) const;
		// Binds the mesh to the skeleton asset (the target is its data): the skin's joint indices are remapped to the target's joint order and the
		// recorded joint names become the target's. Refused (false, missing says why) when can_rebind is false.
		bool rebind(const AssetRef& skeleton, const SkeletonData& target, std::vector<std::string>& missing);

		// Bakes levels of detail (the ones the mesh had are replaced) or removes them. The simplifier runs on the calling thread, a second or two for a big mesh.
		LodBakeResult bake_lods(const LodBakeSettings& settings);
		bool clear_lods();

		void undo();
		void redo();
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		bool dirty() const { return m_History.dirty(); }
		void mark_saved() { m_History.mark_saved(); }

	private:
		// What can change; the skin is shared between snapshots that did not touch it.
		struct State
		{
			std::vector<AssetRef> Materials{};
			AssetRef Skeleton{};
			std::vector<std::string> JointNames{};
			std::shared_ptr<const std::vector<MeshSkinVertex>> Skin{};
			std::shared_ptr<const std::vector<u32>> Indices{};
			std::vector<std::pair<u32, std::array<MeshLod, MAX_MESH_LODS>>> Lods{}; // per primitive: how many levels, and them
		};

		State capture() const;
		void apply(const State& state);

		MeshData m_Mesh;
		std::shared_ptr<const std::vector<MeshSkinVertex>> m_Skin;
		std::shared_ptr<const std::vector<u32>> m_Indices;
		EditHistory<State> m_History;
	};
}
