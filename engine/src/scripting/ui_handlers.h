#pragma once

#include <vector>

#include <sol/sol.hpp>

#include <scene/entity_guid.h>
#include <ui/ui_interaction.h>

namespace wiwa
{
	// One Lua function that listens to one event of one widget. Owner is the `self` of the script instance that registered it: the
	// function is called with it first, and goes away with the instance.
	struct UiHandler
	{
		EntityGuid Canvas{}; // the entity of the UICanvas
		UiId Widget = 0;
		UiEventKind Kind = UiEventKind::Click;
		sol::protected_function Function{};
		sol::table Owner{};
	};

	// The listeners of the scripts running on one scene. Main thread only.
	class UiHandlerTable
	{
	public:
		void add(UiHandler handler);

		// Removes the handlers the owner registered for this widget and event; with a function (not nil) only that one. Returns how many.
		size_t remove(const sol::table& owner, const EntityGuid& canvas, UiId widget, UiEventKind kind, const sol::object& function);
		void remove_owner(const sol::table& owner);
		void clear();

		// A copy, so that a handler may add and remove handlers while it is called.
		std::vector<UiHandler> matching(const EntityGuid& canvas, UiId widget, UiEventKind kind) const;
		bool empty() const { return m_Handlers.empty(); }

		// The `self` of the script hook that is running, null outside a hook. Handlers are registered for it.
		void set_current(const sol::table* self) { m_Current = self; }
		const sol::table* current() const { return m_Current; }

	private:
		std::vector<UiHandler> m_Handlers{};
		const sol::table* m_Current = nullptr;
	};
}
