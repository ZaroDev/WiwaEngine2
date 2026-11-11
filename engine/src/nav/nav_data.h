#pragma once

#include <array>
#include <string>

#include <core/common.h>
#include "nav_mesh_asset.h"
#include "nav_types.h"

namespace wiwa
{
	// Which areas a walker may use and what each costs (the project's costs, see NavSettings, narrowed by the agent's area mask).
	struct NavFilter
	{
		u32 AreaMask = 0xFFFFFFFFu;
		std::array<float, NAV_AREA_COUNT> AreaCost = NavSettings{}.AreaCost;
	};

	// A navigation mesh ready to be queried: the Detour mesh made from a NavMeshData. Immutable after create(), so any number of queries and a crowd
	// may read it (each query object on one thread). No Detour type appears here; nav_data_impl.h is for the nav module's own files.
	class NavData
	{
	public:
		struct Impl;

		// Null, and why in error, for a mesh that is not valid() or whose tiles Detour refuses. The tiles come from our own bake: Detour checks
		// each tile's header, not every offset inside it, so a navmesh file is as trusted as the editor that wrote it.
		static Scope<NavData> create(const NavMeshData& mesh, std::string& error);
		~NavData();

		NavData(const NavData&) = delete;
		NavData& operator=(const NavData&) = delete;

		const NavMeshData& source() const { return m_Source; }
		Impl& impl() const { return *m_Impl; }

	private:
		NavData();

		NavMeshData m_Source{};
		Scope<Impl> m_Impl{};
	};
}
