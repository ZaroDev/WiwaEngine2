#include <pch.h>
#include "ui_handlers.h"

#include <algorithm>

namespace wiwa
{
	void UiHandlerTable::add(UiHandler handler)
	{
		m_Handlers.push_back(std::move(handler));
	}

	size_t UiHandlerTable::remove(const sol::table& owner, const EntityGuid& canvas, UiId widget, UiEventKind kind, const sol::object& function)
	{
		const bool any = !function.valid() || function.get_type() == sol::type::lua_nil || function.get_type() == sol::type::none;
		return std::erase_if(m_Handlers, [&](const UiHandler& handler)
			{
				return handler.Owner == owner && handler.Canvas == canvas && handler.Widget == widget && handler.Kind == kind
					&& (any || handler.Function == function);
			});
	}

	void UiHandlerTable::remove_owner(const sol::table& owner)
	{
		std::erase_if(m_Handlers, [&](const UiHandler& handler) { return handler.Owner == owner; });
	}

	void UiHandlerTable::clear()
	{
		m_Handlers.clear();
	}

	std::vector<UiHandler> UiHandlerTable::matching(const EntityGuid& canvas, UiId widget, UiEventKind kind) const
	{
		std::vector<UiHandler> result;
		for (const UiHandler& handler : m_Handlers)
		{
			if (handler.Canvas == canvas && handler.Widget == widget && handler.Kind == kind)
			{
				result.push_back(handler);
			}
		}
		return result;
	}
}
