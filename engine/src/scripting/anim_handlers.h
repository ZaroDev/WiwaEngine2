#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <scene/entity_guid.h>

namespace wiwa
{
	// One Lua function that listens to one clip event of one animator. Owner is the `self` of the script instance that registered it: the
	// function is called with it first, and goes away with the instance.
	struct AnimHandler
	{
		EntityGuid Animator{}; // the entity of the Animator
		std::string Event{};
		sol::protected_function Function{};
		sol::table Owner{};
	};

	// The listeners of the scripts running on one scene. Main thread only.
	class AnimHandlerTable
	{
	public:
		void add(AnimHandler handler);
		// Removes the handlers the owner registered for this animator and event. Returns how many.
		size_t remove(const sol::table& owner, const EntityGuid& animator, const std::string& event);
		// One handler, after it failed.
		void remove_function(const AnimHandler& handler);
		void remove_owner(const sol::table& owner);
		void clear();

		// A copy, so that a handler may add and remove handlers while it is called.
		std::vector<AnimHandler> matching(const EntityGuid& animator, const std::string& event) const;
		bool empty() const { return m_Handlers.empty(); }

	private:
		std::vector<AnimHandler> m_Handlers{};
	};
}
