#pragma once

#include <memory>

#include <imgui.h>

#include <assets/particle_effect.h>

namespace wiwa
{
	// A graph of the curve of a ParticleValue (time 0..1 across, the value up): drag a key to move it, double click on the curve to add one. The keys stay
	// sorted and inside 0..1; the table is baked again after every change. Keys are removed in the key list of the editor, not here.
	class VfxCurveView
	{
	public:
		VfxCurveView();
		~VfxCurveView();

		// Draws `value` (which must be in Curve mode). Returns true when the keys changed; dragging is true while a key is held (the change is one undo step).
		bool draw(ParticleValue& value, ImVec2 size, bool& dragging);
		// The value range is fitted to the keys again the next time it is drawn.
		void fit() { m_Fit = true; }

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
		bool m_Fit = true;
	};

	// Sorts keys by time and clamps them into 0..1 (the same time twice is nudged apart); the rules a curve of a file has to follow.
	void normalize_particle_keys(std::vector<ParticleCurveKey>& keys);
}
