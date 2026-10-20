#pragma once
#include <glm/glm.hpp>
#include <stdint.h>

#pragma once
#ifdef WI_PLATFORM_WINDOWS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_XYZW_ONLY
#endif
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wiwa
{
	//! Signed byte type
	typedef char Byte;
	//! Unsigned byte type
	typedef unsigned char Ubyte;

	// Signed types

	//! Singed integer 8 bit type
	typedef signed __int8 i8;
	//! Singed integer 16 bit type
	typedef signed __int16 i16;
	//! Singed integer 32 bit type
	typedef signed __int32 i32;
	//! Singed integer 64 bit type
	typedef signed __int64 i64;

	constexpr i8    I8_MAX = INT8_MAX;		/**< Maximum value for a signed 8 bit integer */
	constexpr i16   I16_MAX = INT16_MAX;	/**< Maximum value for a signed 16 bit integer */
	constexpr i32   I32_MAX = INT32_MAX;	/**< Maximum value for a signed 32 bit integer */
	constexpr i64   I64_MAX = INT64_MAX;	/**< Maximum value for a signed 64 bit integer */

	// Unsigned
	//! Unsigned integer 8 bit type
	typedef unsigned __int8     u8;
	//! Unsigned integer 16 bit type
	typedef unsigned __int16    u16;
	//! Unsigned integer 32 bit type
	typedef unsigned __int32    u32;
	//! Unsigned integer 64 bit type
	typedef unsigned __int64    u64;

	constexpr u8    U8_MAX = UINT8_MAX;		/**< Maximum value for an unsigned 8 bit integer */
	constexpr u16   U16_MAX = UINT16_MAX;	/**< Maximum value for an unsigned 16 bit integer */
	constexpr u32   U32_MAX = UINT32_MAX;	/**< Maximum value for an unsigned 32 bit integer */
	constexpr u64   U64_MAX = UINT64_MAX;	/**< Maximum value for an unsigned 64 bit integer */

	// Floating pointer type

	//! Singed floating pointer 32 bit type
	typedef float f32;

	//! Singed floating pointer 64 bit type
	typedef double f64;

	typedef size_t SizeT;

	namespace math
	{
		constexpr f32 c_Pi = 3.14159265359f; /**< Pi constant value */
		constexpr f32 c_Epsilon = 1e-5f;  /**< Epsilon constant value */
	}

	//! 2D Vector; 32 bit floating point components
	using V2 = glm::vec2;

	//! 2D Vector; 32 bit integer components 
	using Iv2 = glm::ivec2;

	//! 3D Vector; 32 bit floating point components
	using V3 = glm::vec3;

	//! 3D Vector; 32 bit integer components 
	using Iv3 = glm::ivec3;

	//! 4D Vector; 32 bit floating point components
	using V4 = glm::vec4;

	//! 4D Vector; 32 bit integer components 
	using Iv4 = glm::ivec4;

	//! 2D Vector; 32 bit unsigned integer components
	using U32V2 = glm::u32vec2;

	//! 3D Vector; 32 bit unsigned integer components
	using U32V3 = glm::u32vec3;

	//! 4D Vector; 32 bit unsigned integer components
	using U32V4 = glm::u32vec4;

	//! 2D Vector; 32 bit signed integer components
	using S32V2 = glm::i32vec2;

	//! 3D Vector; 32 bit signed integer components
	using S32V3 = glm::i32vec3;

	//! 4D Vector; 32 bit signed integer components
	using S32V4 = glm::i32vec4;

	//! 3x3 Matrix: 32 bit floating point components
	using Mat3 = glm::mat3x3;

	// 4x4 Matrix (assumes right-handed coordinates)
	using Mat4 = glm::mat4x4;

	//! Quaternion structure
	typedef glm::quat Quaternion;

	//! Color structure
	using Color = glm::vec4;

	using Color3 = V3;
	using Color4 = V4;
}