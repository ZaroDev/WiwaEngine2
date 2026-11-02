#pragma once

#include "scene.h"

namespace wiwa
{
	// A deep copy of a scene, for running it without touching the original (the editor's play mode). Same entity guids, hierarchy, sibling
	// and root order; every reflected component, the value bags of scripts, prefab instances with their overrides, the PrefabChild tags,
	// the unknown components and the scene settings. World transforms are computed again. Main thread only.
	Ref<Scene> clone_scene(Scene& source);
}
