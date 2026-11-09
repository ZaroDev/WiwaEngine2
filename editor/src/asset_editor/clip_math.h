#pragma once

#include <cstddef>

#include <assets/asset_data.h>

// The maths the clip editor shares between its model, its curves and its preview: reading and writing the keys of a channel, evaluating a channel
// exactly as the runtime does (engine/src/animation/pose_nodes.cpp), and quaternion/Euler conversion. Quaternions in channels are stored x y z w.
namespace wiwa
{
	// Floats per key (Components, times 3 for a cubic spline: in-tangent, value, out-tangent).
	size_t channel_stride(const AnimationChannel& channel);
	// Whether the channel's arrays are consistent (what read_clip_file guarantees): keys, components and values fit.
	bool channel_valid(const AnimationChannel& channel);

	// The value (x y z, w for a quaternion) of key `key`; the in and out tangents of a cubic-spline key.
	glm::vec4 key_value(const AnimationChannel& channel, size_t key);
	glm::vec4 key_in_tangent(const AnimationChannel& channel, size_t key);
	glm::vec4 key_out_tangent(const AnimationChannel& channel, size_t key);
	void set_key_value(AnimationChannel& channel, size_t key, const glm::vec4& value);
	void set_key_tangents(AnimationChannel& channel, size_t key, const glm::vec4& in, const glm::vec4& out);

	// Inserts a key at `time` (sorted) with this value (and zero tangents for a cubic spline); returns its index. Replaces a key at the same time.
	size_t insert_key_at(AnimationChannel& channel, float time, const glm::vec4& value);
	void erase_key(AnimationChannel& channel, size_t key);

	// The channel at a time, as the runtime evaluates it (a channel with no keys gives zero).
	glm::vec4 sample_channel(const AnimationChannel& channel, float time);

	// Converts the interpolation of a channel, keeping the keys: to or from a cubic spline the tangents are added (zero) or dropped.
	void set_channel_interpolation(AnimationChannel& channel, AnimationInterpolation interpolation);

	size_t channel_bytes(const AnimationChannel& channel);
	size_t clip_bytes(const AnimationClipData& clip);

	// The quaternion negated when it points away from `previous`, so a rotation curve between keys takes the short way.
	glm::quat quat_continuous(const glm::quat& q, const glm::quat& previous);
	// Euler angles (degrees, x then y then z) of the quaternion: of the triples that make it, the one closest to `reference`, so a curve does not flip at +-180.
	glm::vec3 quat_to_euler_degrees(const glm::quat& q, const glm::vec3& reference);
	glm::quat euler_degrees_to_quat(const glm::vec3& degrees);
}
