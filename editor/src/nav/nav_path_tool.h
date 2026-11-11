#pragma once

#include <optional>

#include <glm/glm.hpp>

#include <nav/nav_query.h>
#include <nav/nav_view.h>

namespace wiwa
{
	class DebugDraw;

	// The path test of the Navigation panel: a start and an end picked on the navmesh in the viewport, and the route Detour finds between them. Edit time
	// only; no ImGui, no engine module.
	class NavPathTool
	{
	public:
		// The nearest point where the ray meets a polygon of the view; nullopt if it misses the mesh.
		static std::optional<glm::vec3> pick(const NavMeshView& view, glm::vec3 rayOrigin, glm::vec3 rayDirection);

		void set_start(glm::vec3 point) { m_Start = point; }
		void set_end(glm::vec3 point) { m_End = point; }
		void clear();
		bool has_start() const { return m_Start.has_value(); }
		bool has_end() const { return m_End.has_value(); }

		// Finds the route when both points are set (the path is empty and failed otherwise).
		void recompute(const NavQuery& query, const NavFilter& filter);
		const NavPath& path() const { return m_Path; }

		void draw(DebugDraw& draw) const;

	private:
		std::optional<glm::vec3> m_Start{};
		std::optional<glm::vec3> m_End{};
		NavPath m_Path{};
	};
}
