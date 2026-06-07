#pragma once
#include "LogEntry.h"
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <vector>
#include <string>
#include <optional>
#include <regex>
#include <functional>

// Virtual list control showing parsed log entries (DLT-style)
class LogTableCtrl : public wxListCtrl
{
public:
    LogTableCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);

    // Add a raw log line; parsed automatically
    void AppendLine(const std::string& line);

    // Replace all entries (after filter change)
    void SetEntries(const std::vector<LogEntry>& entries);

    // Clear everything
    void ClearAll();

    // Attach the payload text ctrl shown below the table
    void SetPayloadCtrl(wxTextCtrl* ctrl) { m_payloadCtrl = ctrl; }

    // Write custom payload text for the selected row
    void SetPayload(const std::string& text);

    // Apply regex filter; returns number of visible rows
    std::size_t ApplyFilter(const std::string& pattern);
    void        ClearFilter();

    const std::vector<LogEntry>& AllEntries()     const { return m_all; }
    const std::vector<LogEntry>& VisibleEntries() const { return m_visible; }

    // Called when user selects a row (optional external hook)
    std::function<void(const LogEntry&)> onRowSelected;

protected:
    wxString OnGetItemText(long item, long col) const override;

private:
    void OnItemSelected(wxListEvent& evt);
    void ShowPayload(long item);

    std::vector<LogEntry>   m_all;      // every received entry
    std::vector<LogEntry>   m_visible;  // after filter
    std::optional<std::regex> m_regex;

    wxTextCtrl* m_payloadCtrl{nullptr};

    wxDECLARE_EVENT_TABLE();
};
