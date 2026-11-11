// The one translation unit that holds the miniaudio implementation (engine/vendor/miniaudio, public domain / MIT-0). It does not use the precompiled
// header (premake turns it off for this file) and nothing else defines MINIAUDIO_IMPLEMENTATION.
// stb_vorbis (public domain / MIT) first: miniaudio then decodes Ogg Vorbis with it.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

// the stb_vorbis implementation, after miniaudio so that its declarations are already seen
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
