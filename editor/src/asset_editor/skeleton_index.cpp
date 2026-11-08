#include <editor_common.h>
#include "skeleton_index.h"

#include <asset_pipeline/asset_database.h>

namespace wiwa
{
	SkeletonIndex::~SkeletonIndex()
	{
		m_Cancel = true;
		if (m_Task.valid())
		{
			m_Task.wait();
		}
	}

	void SkeletonIndex::update()
	{
		if (m_Task.valid())
		{
			if (m_Task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				return;
			}
			m_Index = m_Task.get();
			m_Ready = true;
			m_Generation++;
		}
		if (m_Dirty->exchange(false))
		{
			m_Task = std::async(std::launch::async, [this]() { return index_skeleton_users(m_Database, &m_Cancel); });
		}
	}

	SkeletonUsers SkeletonIndex::users_of(AssetGuid skeleton) const
	{
		const auto it = m_Index.find(skeleton);
		return it != m_Index.end() ? it->second : SkeletonUsers{};
	}
}
