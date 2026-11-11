#pragma once

#include <string>

#include "panel.h"

// The "Audio" panel: which backend plays (and why it fell back to Null, if it did), the loaded soundbanks, how many events play, and the event
// browser built from the banks' metadata (SoundbanksInfo.json), with a search box and an Audition button that plays an event without the game
// running. Auditioning stops when the panel closes and while a scene plays. Main thread, between the ImGui frame's begin and end.
class AudioPanel final : public Panel
{
public:
	AudioPanel();

	virtual void on_update() override;
	virtual void on_draw() override;

	// Plays an event for trying it (what the Audition button does). False if nothing started (a scene plays, or the backend refused the event).
	bool audition(const std::string& event);
	void stop_audition();
	bool auditioning() const { return m_Auditioning; }

	wiwa::u32 frames_drawn() const { return m_Frames; }
	// Brings the window to the front of its dock on the next frame (the smoke check wants the contents drawn).
	void request_focus() { m_Focus = true; }

private:
	char m_Filter[128] = "";
	std::string m_Playing{}; // the event being auditioned
	bool m_Auditioning = false;
	wiwa::u32 m_Frames = 0;
	bool m_Focus = false;
};
