#pragma once

// The only place that includes Jolt first. Jolt headers are included from engine/src/physics only.
// The JPH_* defines come from the premake files (JoltDefines) and must match the Jolt library.
#include <Jolt/Jolt.h>

static_assert(sizeof(JPH::Real) == sizeof(float), "Jolt must be built with single precision");
