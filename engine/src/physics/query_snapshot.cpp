#include <pch.h>
#include "query_snapshot.h"
#include "query_snapshot_jolt.h"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

#include "shape_builder.h"

namespace wiwa
{
	namespace
	{
		JPH::Vec3 to_jolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat to_jolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		bool in_mask(const QueryBody& body, u32 mask) { return ((mask >> std::min<u32>(body.Layer, 31u)) & 1u) != 0; }

		// The body's frame (origin = body position, shape space) and its centre of mass frame.
		JPH::Mat44 body_transform(const QueryBody& body) { return JPH::Mat44::sRotationTranslation(to_jolt(body.Rotation), to_jolt(body.Position)); }
		JPH::Mat44 com_transform(const QueryBody& body) { return body_transform(body) * JPH::Mat44::sTranslation(body.Shape->GetCenterOfMass()); }

		template<typename F>
		void for_each_body(const QuerySnapshot::Impl& impl, u32 mask, F&& visit)
		{
			if (impl.Static != nullptr)
			{
				for (const QueryBody& body : *impl.Static)
				{
					if (in_mask(body, mask))
					{
						visit(body);
					}
				}
			}
			for (const QueryBody& body : impl.Moving)
			{
				if (in_mask(body, mask))
				{
					visit(body);
				}
			}
		}

		// The ray's hits on one body, as RayHits (the ray is `length` long).
		void ray_on_body(const QueryBody& body, glm::vec3 origin, glm::vec3 unit, float length, std::vector<RayHit>& hits, bool closestOnly)
		{
			const JPH::Mat44 world = body_transform(body);
			const JPH::Mat44 inverse = world.InversedRotationTranslation();
			const JPH::Vec3 localOrigin = inverse * to_jolt(origin);
			const JPH::Vec3 localDirection = inverse.Multiply3x3(to_jolt(unit)) * length;
			const JPH::RayCast ray{ localOrigin, localDirection };

			const auto add = [&](float fraction, const JPH::SubShapeID& subShape)
			{
				const JPH::Vec3 localPoint = ray.GetPointOnRay(fraction);
				RayHit hit;
				hit.Key = body.Key;
				hit.Distance = fraction * length;
				hit.Point = to_glm(world * localPoint);
				hit.Normal = to_glm(world.Multiply3x3(body.Shape->GetSurfaceNormal(subShape, localPoint)).Normalized());
				hits.push_back(hit);
			};

			if (closestOnly)
			{
				JPH::RayCastResult result;
				if (body.Shape->CastRay(ray, JPH::SubShapeIDCreator(), result))
				{
					add(result.mFraction, result.mSubShapeID2);
				}
				return;
			}

			JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
			body.Shape->CastRay(ray, JPH::RayCastSettings(), JPH::SubShapeIDCreator(), collector);
			for (const JPH::RayCastResult& result : collector.mHits)
			{
				add(result.mFraction, result.mSubShapeID2);
			}
		}

		bool overlaps(const QueryBody& body, const JPH::Shape& probe, const JPH::Mat44& probeCom)
		{
			JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
			JPH::CollisionDispatch::sCollideShapeVsShape(&probe, body.Shape, JPH::Vec3::sReplicate(1.0f), JPH::Vec3::sReplicate(1.0f), probeCom, com_transform(body),
				JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), JPH::CollideShapeSettings(), collector);
			return collector.HadHit();
		}

		bool usable_ray(glm::vec3 origin, glm::vec3 direction, float maxDistance)
		{
			return finite(origin) && finite(direction) && glm::length(direction) > 1e-9f && maxDistance > 0.0f && std::isfinite(maxDistance);
		}
	}

	QuerySnapshot::QuerySnapshot(std::unique_ptr<Impl> impl) : m_Impl(std::move(impl)) {}
	QuerySnapshot::~QuerySnapshot() = default;

	size_t QuerySnapshot::body_count() const
	{
		return (m_Impl->Static != nullptr ? m_Impl->Static->size() : 0) + m_Impl->Moving.size();
	}

	std::optional<RayHit> QuerySnapshot::raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance, u32 layerMask) const
	{
		if (!usable_ray(origin, direction, maxDistance))
		{
			return std::nullopt;
		}

		std::vector<RayHit> hits;
		const glm::vec3 unit = glm::normalize(direction);
		for_each_body(*m_Impl, layerMask, [&](const QueryBody& body) { ray_on_body(body, origin, unit, maxDistance, hits, true); });
		if (hits.empty())
		{
			return std::nullopt;
		}
		return *std::min_element(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.Distance < b.Distance; });
	}

	std::vector<RayHit> QuerySnapshot::raycast_all(glm::vec3 origin, glm::vec3 direction, float maxDistance, u32 layerMask) const
	{
		std::vector<RayHit> hits;
		if (!usable_ray(origin, direction, maxDistance))
		{
			return hits;
		}
		const glm::vec3 unit = glm::normalize(direction);
		for_each_body(*m_Impl, layerMask, [&](const QueryBody& body) { ray_on_body(body, origin, unit, maxDistance, hits, false); });
		std::stable_sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.Distance < b.Distance; });
		return hits;
	}

	std::vector<BodyKey> QuerySnapshot::overlap_sphere(glm::vec3 center, float radius, u32 layerMask) const
	{
		std::vector<BodyKey> keys;
		if (!finite(center) || !(radius > 0.0f) || !std::isfinite(radius))
		{
			return keys;
		}
		const JPH::SphereShape probe(radius);
		const JPH::Mat44 transform = JPH::Mat44::sTranslation(to_jolt(center));
		for_each_body(*m_Impl, layerMask, [&](const QueryBody& body)
		{
			if (overlaps(body, probe, transform))
			{
				keys.push_back(body.Key);
			}
		});
		std::sort(keys.begin(), keys.end());
		return keys;
	}

	std::vector<BodyKey> QuerySnapshot::overlap_box(glm::vec3 center, glm::vec3 halfExtents, glm::quat rotation, u32 layerMask) const
	{
		std::vector<BodyKey> keys;
		const float rotationLength = glm::length(rotation);
		if (!finite(center) || !finite(halfExtents) || std::min({ halfExtents.x, halfExtents.y, halfExtents.z }) <= 0.0f || !(rotationLength > 1e-6f))
		{
			return keys;
		}
		const JPH::BoxShape probe(to_jolt(halfExtents), std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({ halfExtents.x, halfExtents.y, halfExtents.z })));
		const JPH::Mat44 transform = JPH::Mat44::sRotationTranslation(to_jolt(rotation / rotationLength), to_jolt(center));
		for_each_body(*m_Impl, layerMask, [&](const QueryBody& body)
		{
			if (overlaps(body, probe, transform))
			{
				keys.push_back(body.Key);
			}
		});
		std::sort(keys.begin(), keys.end());
		return keys;
	}

	std::optional<RayHit> QuerySnapshot::shape_cast(const ShapeDesc& desc, glm::vec3 from, glm::vec3 to, u32 layerMask) const
	{
		const glm::vec3 delta = to - from;
		const float length = glm::length(delta);
		if (!finite(from) || !finite(to) || !(length > 1e-9f))
		{
			return std::nullopt;
		}

		ShapeBuilder builder;
		const JPH::RefConst<JPH::Shape> probe = builder.build(desc);
		if (probe == nullptr)
		{
			return std::nullopt;
		}

		const JPH::ShapeCast cast = JPH::ShapeCast::sFromWorldTransform(probe, JPH::Vec3::sReplicate(1.0f), JPH::Mat44::sTranslation(to_jolt(from)), to_jolt(delta));
		JPH::ShapeCastSettings settings;
		settings.mReturnDeepestPoint = true;

		std::optional<RayHit> best;
		for_each_body(*m_Impl, layerMask, [&](const QueryBody& body)
		{
			JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
			JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(cast, settings, body.Shape, JPH::Vec3::sReplicate(1.0f), JPH::ShapeFilter(), com_transform(body),
				JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), collector);
			if (!collector.HadHit())
			{
				return;
			}
			const JPH::ShapeCastResult& hit = collector.mHit;
			if (!best || hit.mFraction * length < best->Distance)
			{
				RayHit result;
				result.Key = body.Key;
				result.Distance = hit.mFraction * length;
				result.Point = to_glm(hit.mContactPointOn2);
				const JPH::Vec3 axis = hit.mPenetrationAxis;
				result.Normal = axis.Length() > 1e-9f ? -to_glm(axis.Normalized()) : glm::vec3(0.0f);
				best = result;
			}
		});
		return best;
	}
}
