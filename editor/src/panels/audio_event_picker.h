#pragma once

#include <string>

// A text box for the name of an audio event, with a button that lists the events of the loaded banks (a search box and a list to click). A name no
// loaded bank lists is kept (the banks may simply not be loaded yet) and marked with a warning. True if the name changed.
bool draw_audio_event_picker(const char* label, std::string& event);
