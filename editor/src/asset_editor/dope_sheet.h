#pragma once

#include <set>
#include <string>

#include <imgui.h>

#include "clip_edit_model.h"

namespace wiwa
{
	struct DopeSheetOptions
	{
		bool Snap = true;          // keys and the playhead snap to frames
		float SnapFps = 30.0f;
		const SkeletonData* Skeleton = nullptr; // joints the clip may name (paste is refused for others); may be null
	};

	// The keys of a clip as rows: a time ruler with the playhead, an event lane, a row per joint (collapsed: the keys of all its channels; open: a row
	// each for translation, rotation and scale), one diamond per key. Click selects (Shift/Ctrl adds), a box selects across rows, dragging moves the
	// selected keys in time, Alt-drag scales them about the playhead, Ctrl+C / Ctrl+V / Delete copy, paste and delete, Ctrl+wheel zooms the time axis
	// and the middle button pans it. Edits go through ClipEditModel (a drag is one undo step). Main thread, inside an ImGui window.
	class DopeSheet
	{
	public:
		// Returns true on the frames the user moved the playhead (a click or drag on the ruler); playhead is then the new time.
		bool draw(ClipEditModel& model, float& playhead, KeySelection& selection, const DopeSheetOptions& options, ImVec2 size);

		const std::string& selected_joint() const { return m_SelectedJoint; }
		// The time axis shows the whole clip again on the next draw (a new clip was opened).
		void fit() { m_Fit = true; m_Collapsed.clear(); m_Opened.clear(); }
		const KeyClipboard& clipboard() const { return m_Clipboard; }

	private:
		enum class Drag { None, Keys, Scale, Box, Event };

		float time_to_x(float time) const { return m_TimeOrigin + (time - m_Start) * m_PixelsPerSecond; }
		float x_to_time(float x) const { return m_Start + (x - m_TimeOrigin) / m_PixelsPerSecond; }
		float snapped(float time, const DopeSheetOptions& options) const;

		float m_Start = 0.0f;               // the time at the left edge of the time area
		float m_PixelsPerSecond = 100.0f;
		float m_TimeOrigin = 0.0f;          // screen x of the left edge of the time area (this frame)
		bool m_Fit = true;
		std::string m_SelectedJoint{};
		int m_SelectedEvent = -1;
		std::set<std::string> m_Opened{};   // joints whose channels are shown
		std::set<std::string> m_Collapsed{};
		KeyClipboard m_Clipboard{};

		Drag m_Drag = Drag::None;
		ImVec2 m_DragStart{};
		float m_GrabTime = 0.0f;            // the time of the key that was grabbed
		float m_AppliedShift = 0.0f;        // how far the selection has moved since the drag began
		float m_ScaleReference = 1.0f;
		bool m_ScrubbingRuler = false;
		bool m_PanDrag = false;
		bool m_RenameRequested = false;
		bool m_RenameFocus = false;
		char m_RenameBuffer[80] = {};
	};
}
