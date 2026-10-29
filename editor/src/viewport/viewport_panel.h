#pragma once

// The "Viewport" panel: the scene as the renderer draws it, filling the window. It tells the renderer how big its content region is
// (the scene is rendered at exactly that size) and shows the result as an image. Main thread, between the ImGui frame's begin and end.
class ViewportPanel
{
public:
	void draw();
};
