#include <pch.h>
#include "nav_query.h"
#include "nav_data_impl.h"

#include <algorithm>
#include <cmath>

#include <DetourNavMeshQuery.h>

namespace wiwa
{
	namespace
	{
		constexpr int MAX_POLYS = 256;
		constexpr int MAX_STRAIGHT = 256;

		thread_local u32 t_RandomState = 0x9E3779B9u;

		float next_random()
		{
			// xorshift32, mapped to [0, 1)
			u32 x = t_RandomState;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			t_RandomState = x;
			return static_cast<float>(x >> 8) / 16777216.0f;
		}

		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		bool usable(const glm::vec3& v) { return finite(v) && glm::all(glm::lessThan(glm::abs(v), glm::vec3(1.0e7f))); }
		bool usable_extents(const glm::vec3& v) { return usable(v) && glm::all(glm::greaterThanEqual(v, glm::vec3(0.0f))); }
	}

	struct NavQuery::Impl
	{
		dtNavMeshQuery* Query = nullptr;

		~Impl()
		{
			dtFreeNavMeshQuery(Query);
		}
	};

	NavQuery::NavQuery(const NavData& data) : m_Impl(CreateScope<Impl>())
	{
		m_Impl->Query = dtAllocNavMeshQuery();
		if (m_Impl->Query != nullptr && dtStatusFailed(m_Impl->Query->init(data.impl().Mesh, 2048)))
		{
			dtFreeNavMeshQuery(m_Impl->Query);
			m_Impl->Query = nullptr;
		}
	}

	NavQuery::~NavQuery() = default;

	void NavQuery::seed_random(u32 seed)
	{
		t_RandomState = seed != 0 ? seed : 0x9E3779B9u;
	}

	NavPath NavQuery::find_path(glm::vec3 from, glm::vec3 to, const NavFilter& filter, glm::vec3 extents) const
	{
		NavPath path;
		if (m_Impl->Query == nullptr || !usable(from) || !usable(to) || !usable_extents(extents))
		{
			return path;
		}

		dtQueryFilter detourFilter;
		fill_detour_filter(detourFilter, filter);

		dtPolyRef startRef = 0;
		dtPolyRef endRef = 0;
		glm::vec3 start{ 0.0f };
		glm::vec3 end{ 0.0f };
		m_Impl->Query->findNearestPoly(&from.x, &extents.x, &detourFilter, &startRef, &start.x);
		m_Impl->Query->findNearestPoly(&to.x, &extents.x, &detourFilter, &endRef, &end.x);
		if (startRef == 0 || endRef == 0)
		{
			return path;
		}

		dtPolyRef polys[MAX_POLYS];
		int polyCount = 0;
		const dtStatus status = m_Impl->Query->findPath(startRef, endRef, &start.x, &end.x, &detourFilter, polys, &polyCount, MAX_POLYS);
		if (dtStatusFailed(status) || polyCount == 0)
		{
			return path;
		}

		// The route may stop short of the target: then the walk ends at the point of the last polygon nearest to it.
		const bool partial = polys[polyCount - 1] != endRef;
		glm::vec3 last = end;
		if (partial)
		{
			bool over = false;
			if (dtStatusFailed(m_Impl->Query->closestPointOnPoly(polys[polyCount - 1], &end.x, &last.x, &over)))
			{
				return path;
			}
		}

		glm::vec3 corners[MAX_STRAIGHT];
		unsigned char flags[MAX_STRAIGHT];
		dtPolyRef refs[MAX_STRAIGHT];
		int cornerCount = 0;
		if (dtStatusFailed(m_Impl->Query->findStraightPath(&start.x, &last.x, polys, polyCount, &corners[0].x, flags, refs, &cornerCount, MAX_STRAIGHT, 0)) || cornerCount == 0)
		{
			return path;
		}

		path.Corners.assign(corners, corners + cornerCount);
		for (int i = 1; i < cornerCount; i++)
		{
			path.Length += glm::distance(corners[i - 1], corners[i]);
		}
		path.Status = (partial || (dtStatusDetail(status, DT_PARTIAL_RESULT) != 0)) ? NavPathStatus::Partial : NavPathStatus::Complete;
		return path;
	}

	std::optional<glm::vec3> NavQuery::nearest_point(glm::vec3 point, glm::vec3 extents, const NavFilter& filter) const
	{
		if (m_Impl->Query == nullptr || !usable(point) || !usable_extents(extents))
		{
			return std::nullopt;
		}
		dtQueryFilter detourFilter;
		fill_detour_filter(detourFilter, filter);
		dtPolyRef ref = 0;
		glm::vec3 nearest{ 0.0f };
		m_Impl->Query->findNearestPoly(&point.x, &extents.x, &detourFilter, &ref, &nearest.x);
		if (ref == 0)
		{
			return std::nullopt;
		}
		return nearest;
	}

	bool NavQuery::raycast(glm::vec3 a, glm::vec3 b, const NavFilter& filter, glm::vec3* hit) const
	{
		if (m_Impl->Query == nullptr || !usable(a) || !usable(b))
		{
			return false;
		}
		dtQueryFilter detourFilter;
		fill_detour_filter(detourFilter, filter);

		const glm::vec3 extents = NAV_PROBE_EXTENTS;
		dtPolyRef startRef = 0;
		glm::vec3 start{ 0.0f };
		glm::vec3 probe = extents;
		m_Impl->Query->findNearestPoly(&a.x, &probe.x, &detourFilter, &startRef, &start.x);
		if (startRef == 0)
		{
			return false;
		}

		dtRaycastHit result{};
		dtPolyRef path[MAX_POLYS];
		result.path = path;
		result.maxPath = MAX_POLYS;
		if (dtStatusFailed(m_Impl->Query->raycast(startRef, &start.x, &b.x, &detourFilter, 0, &result)))
		{
			return false;
		}
		if (result.t > 1.0f)
		{
			return false; // the line is clear to the end
		}
		if (hit != nullptr)
		{
			*hit = start + (b - start) * result.t;
		}
		return true;
	}

	std::optional<glm::vec3> NavQuery::random_point_around(glm::vec3 centre, float radius, const NavFilter& filter) const
	{
		if (m_Impl->Query == nullptr || !usable(centre) || !std::isfinite(radius) || radius <= 0.0f)
		{
			return std::nullopt;
		}
		dtQueryFilter detourFilter;
		fill_detour_filter(detourFilter, filter);

		glm::vec3 extents = NAV_PROBE_EXTENTS;
		dtPolyRef startRef = 0;
		glm::vec3 start{ 0.0f };
		m_Impl->Query->findNearestPoly(&centre.x, &extents.x, &detourFilter, &startRef, &start.x);
		if (startRef == 0)
		{
			return std::nullopt;
		}

		// Detour picks a point of the polygons the circle touches, which can be far outside it when a polygon is large: such a point is pulled
		// back into the circle and put on the mesh again.
		for (int attempt = 0; attempt < 8; attempt++)
		{
			dtPolyRef randomRef = 0;
			glm::vec3 point{ 0.0f };
			if (dtStatusFailed(m_Impl->Query->findRandomPointAroundCircle(startRef, &start.x, radius, &detourFilter, next_random, &randomRef, &point.x)) || randomRef == 0)
			{
				return std::nullopt;
			}

			const glm::vec2 offset(point.x - start.x, point.z - start.z);
			const float distance = glm::length(offset);
			if (distance <= radius)
			{
				return point;
			}

			const glm::vec2 pulled = glm::vec2(start.x, start.z) + offset * (radius * std::sqrt(next_random()) / distance);
			glm::vec3 candidate(pulled.x, point.y, pulled.y);
			glm::vec3 reach = NAV_PROBE_EXTENTS;
			dtPolyRef candidateRef = 0;
			glm::vec3 snapped{ 0.0f };
			m_Impl->Query->findNearestPoly(&candidate.x, &reach.x, &detourFilter, &candidateRef, &snapped.x);
			if (candidateRef != 0 && glm::distance(glm::vec2(snapped.x, snapped.z), glm::vec2(start.x, start.z)) <= radius + 0.01f)
			{
				return snapped;
			}
		}
		return std::nullopt;
	}

	bool NavQuery::is_walkable(glm::vec3 point, const NavFilter& filter) const
	{
		if (m_Impl->Query == nullptr || !usable(point))
		{
			return false;
		}
		dtQueryFilter detourFilter;
		fill_detour_filter(detourFilter, filter);

		glm::vec3 extents{ 0.05f, 0.5f, 0.05f };
		dtPolyRef ref = 0;
		glm::vec3 nearest{ 0.0f };
		bool over = false;
		m_Impl->Query->findNearestPoly(&point.x, &extents.x, &detourFilter, &ref, &nearest.x, &over);
		return ref != 0 && over;
	}
}
