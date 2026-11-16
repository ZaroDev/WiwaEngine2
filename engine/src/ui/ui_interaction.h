#pragma once

#include <vector>

#include "ui_draw_list.h"
#include "ui_input.h"

// Hover, press, focus and keyboard or gamepad navigation of the interactive widgets (Button, Slider, Toggle) of one panel.
namespace wiwa
{
	enum class UiEventKind : u32
	{
		Click,
		ValueChanged, // a slider moved or a toggle flipped; Value is the new value
		HoverEnter,
		HoverExit,
		Focus,        // the widget got the focus
		Finished,     // a Video widget's video ended (or was stopped, or failed): Skipped and Failed say which; raised by the video module
	};

	struct UiEvent
	{
		UiEventKind Kind = UiEventKind::Click;
		UiId Widget = 0;
		float Value = 0.0f;
		bool Skipped = false; // Finished: a script stopped the video
		bool Failed = false;  // Finished: the video could not play, or broke
	};

	class UiInteraction
	{
	public:
		// One frame. Changes the Value of sliders and toggles in the panel, sets the state of each widget (by index) and appends what
		// happened to events. A widget only takes part while it and its parents are visible and enabled, and the pointer must be inside the
		// rect that clips it. The pointer goes to the widget drawn last; once a press has captured a widget (a button, a slider being
		// dragged) the others see nothing until the release. A click is a release over the widget that got the press.
		// With nothing focused, the first navigation press focuses the first widget; then Up, Down, Left and Right move to the nearest one
		// in that direction (a focused slider takes the two keys of its own axis and moves by one step, or a tenth of its range without
		// one), Submit clicks, Cancel drops the focus.
		void update(UiPanelData& panel, const UiLayoutResult& layout, std::vector<UiInteractionState>& states, const UiInputFrame& input, std::vector<UiEvent>& events);

		UiId hovered() const { return m_Hovered; }
		UiId focused() const { return m_Focused; }
		UiId captured() const { return m_Captured; }

		void reset();

	private:
		UiId m_Hovered = 0;
		UiId m_Focused = 0;
		UiId m_Captured = 0;
	};
}
