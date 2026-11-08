#pragma once

#include <memory>
#include <string>

#include <imgui.h>

#include "clip_edit_model.h"

namespace wiwa
{
	// The channels of one joint as curves (translation X/Y/Z, rotation as Euler X/Y/Z in degrees, scale X/Y/Z), drawn and edited by ImCurveEdit. The x axis
	// is frames at `fps` (ImCurveEdit counts the last frame as inclusive). A point can be dragged in time and value, a double click on a curve adds a key,
	// and edits go through ClipEditModel (a drag is one undo step). ImCurveEdit keeps its own selection: the keys it has selected are copied into
	// `selection` (the dope sheet shows them), but a selection made in the dope sheet does not show here. Main thread, inside an ImGui window.
	class CurveView
	{
	public:
		CurveView();
		~CurveView();
		CurveView(const CurveView&) = delete;
		CurveView& operator=(const CurveView&) = delete;

		void draw(ClipEditModel& model, const std::string& joint, KeySelection& selection, float fps, ImVec2 size);
		// The value range is fitted to the joint's curves again on the next draw.
		void fit() { m_Fit = true; }

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
		std::string m_Joint{};
		bool m_Fit = true;
	};
}
