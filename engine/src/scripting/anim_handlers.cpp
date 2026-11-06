#include <pch.h>
#include "anim_handlers.h"

#include <algorithm>

namespace wiwa
{
	void AnimHandlerTable::add(AnimHandler handler)
	{
		m_Handlers.push_back(std::move(handler));
	}

	size_t AnimHandlerTable::remove(const sol::table& owner, const EntityGuid& animator, const std::string& event)
	{
		return std::erase_if(m_Handlers, [&](const AnimHandler& handler) { return handler.Owner == owner && handler.Animator == animator && handler.Event == event; });
	}

	void AnimHandlerTable::remove_function(const AnimHandler& handler)
	{
		std::erase_if(m_Handlers, [&](const AnimHandler& other)
			{
				return other.Owner == handler.Owner && other.Animator == handler.Animator && other.Event == handler.Event && other.Function == handler.Function;
			});
	}

	void AnimHandlerTable::remove_owner(const sol::table& owner)
	{
		std::erase_if(m_Handlers, [&](const AnimHandler& handler) { return handler.Owner == owner; });
	}

	void AnimHandlerTable::clear()
	{
		m_Handlers.clear();
	}

	std::vector<AnimHandler> AnimHandlerTable::matching(const EntityGuid& animator, const std::string& event) const
	{
		std::vector<AnimHandler> result;
		for (const AnimHandler& handler : m_Handlers)
		{
			if (handler.Animator == animator && handler.Event == event)
			{
				result.push_back(handler);
			}
		}
		return result;
	}
}
