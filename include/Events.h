#pragma once
#include <wx/event.h>

// Fired by background SSH thread -> main thread
// GetInt()    = tab index
// GetString() = log line (UTF-8)
wxDECLARE_EVENT(EVT_SSH_LOG,    wxThreadEvent);

// Fired when the reader starts/stops
// GetInt()    = tab index
// GetString() = "connected" | "disconnected"
wxDECLARE_EVENT(EVT_SSH_STATUS, wxThreadEvent);
