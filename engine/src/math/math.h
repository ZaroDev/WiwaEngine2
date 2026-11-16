#pragma once
#include <glm/glm.hpp>
#include <stdint.h>

#ifdef WI_PLATFORM_WINDOWS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_XYZW_ONLY
#endif
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wiwa
{
	typedef signed __int8 i8;
	typedef signed __int16 i16;
	typedef signed __int32 i32;
	typedef signed __int64 i64;

	constexpr i32   I32_MAX = INT32_MAX;

	typedef unsigned __int8     u8;
	typedef unsigned __int16    u16;
	typedef unsigned __int32    u32;
	typedef unsigned __int64    u64;

	constexpr u32   U32_MAX = UINT32_MAX;

	typedef float f32;
	typedef double f64;

	using V2 = glm::vec2;
	using Iv2 = glm::ivec2;
	using V3 = glm::vec3;
	using V4 = glm::vec4;

	using Mat3 = glm::mat3x3;

	// assumes right-handed coordinates
	using Mat4 = glm::mat4x4;

	typedef glm::quat Quaternion;

	using Color = glm::vec4;
}
