#pragma once

#include <glm/glm.hpp>

// While a scene plays the game reads the keyboard and the mouse as long as the panel that shows it has the focus or the pointer is over it.
// Escape or Shift+F1 hands them back to the editor (typing in the panels works again) until the image is clicked. The Viewport and Game panels
// each own one, and only the one that shows the render calls update(). Call it with the panel's window current, right after its image.
class PlayInputRouter
{
public:
	// imageMin is the top left corner of the image in ImGui coordinates, scale the framebuffer scale.
	void update(bool playing, const glm::vec2& imageMin, float scale, bool hovered);

private:
	bool m_Released = false; // Escape or Shift+F1 gave the keyboard and the mouse back to the editor
};
