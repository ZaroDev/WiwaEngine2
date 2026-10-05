#pragma once

#define OPENGL_DEBUG WI_DEBUG || WI_RELEASE
#define WI_OPENGL_LOG(level, ...) WI_LOG(level, "OpenGL", __VA_ARGS__)