#pragma once
#include <climits>

namespace wiwa
{
	// The update order of every module, lowest first. The one place where the order is decided; a module's get_priority returns one of these, and what a
	// module must run before or after is read here. Values are spaced so a module fits between two others.
	namespace ModulePriority
	{
		constexpr int AssetManager = -100;
		constexpr int AssetPipeline = -90;  // the editor's
		constexpr int Scene = -80;
		constexpr int Render = -50;
		constexpr int Input = -30;          // after the scene and the renderer, before the UI and the scripts
		constexpr int Physics = -20;        // after the scene, before the UI and the scripts
		constexpr int Ui = -10;             // after the scene and the renderer, before the scripts
		constexpr int Scripting = 0;
		constexpr int Editor = 0;           // the editor's module, with the scripts
		constexpr int Nav = 5;              // after the scripts: a destination set this frame is steered in it; before the animation, which reads the moved transforms
		constexpr int AI = 6;               // after the scripts and the navigation (MoveTo sees this frame's crowd step), before the animation
		constexpr int Animation = 10;       // after the scripts
		constexpr int Decal = 11;
		constexpr int Particle = 12;
		constexpr int Video = 13;
		constexpr int Audio = 20;           // after the scripts and the animation: what they did this frame is heard in it
		constexpr int Window = INT_MAX;     // last
	}
}
