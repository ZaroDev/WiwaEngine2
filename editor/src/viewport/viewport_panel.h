#pragma once

#include <renderer/scene/editor_camera.h>

// The "Viewport" panel: the scene as the renderer draws it, filling the window. It tells the renderer how big its content region is
// (the scene is rendered at exactly that size) and shows the result as an image. It also flies a camera of its own: hold the right
// mouse button over the panel and look around with the mouse, WASD to move, Q/E down and up, Shift for speed, the wheel changes the
// speed. With "Editor camera" off the scene's primary camera is used. Main thread, between the ImGui frame's begin and end.
class ViewportPanel
{
public:
	void draw();

private:
	wiwa::EditorCamera m_Camera{};
	bool m_UseEditorCamera = true;
};
