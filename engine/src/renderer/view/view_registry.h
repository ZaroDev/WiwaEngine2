#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "view_types.h"

namespace wiwa
{
	// The names of the views made for ViewHost components, so that a UI widget can name the view it shows. Main thread.
	class ViewRegistry
	{
	public:
		// A name is held by one view; a second view with the same name takes it over.
		void set_name(ViewId id, std::string name)
		{
			remove(id);
			if (!name.empty())
			{
				m_ByName[std::move(name)] = id;
			}
		}

		void remove(ViewId id)
		{
			for (auto it = m_ByName.begin(); it != m_ByName.end();)
			{
				it = it->second == id ? m_ByName.erase(it) : std::next(it);
			}
		}

		// INVALID_VIEW when no view has the name.
		ViewId find(std::string_view name) const
		{
			const auto found = m_ByName.find(std::string(name));
			return found == m_ByName.end() ? INVALID_VIEW : found->second;
		}

		const std::unordered_map<std::string, ViewId>& names() const { return m_ByName; }

	private:
		std::unordered_map<std::string, ViewId> m_ByName{};
	};
}
