#pragma once
#include "LogEntry.h"
#include "FilterConfig.h"
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <vector>
#include <string>
#include <optional>
#include <regex>
#include <functional>

struct RowColour
{
    wxColour bg{255,255,255};
    wxColour fg{0,0,0};
    bool     hasColour{false};
};

class LogTableCtrl : public wxListCtrl
{
public:
    LogTableCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);

    void AppendLine(const std::string& line);
    void SetEntries(const std::vector<LogEntry>& entries);
    void ClearAll();
    void SetPayloadCtrl(wxTextCtrl* ctrl) { m_payloadCtrl = ctrl; }
    void SetPayload(const std::string& text);

    // Quick regex filter (toolbar/inline)
    std::size_t ApplyFilter(const std::string& pattern);
    void        ClearFilter();

    // Coloured filter rules from FilterConfig
    void SetFilterConfig(const FilterConfig* cfg) { m_filterCfg = cfg; Refresh(); }

    const std::vector<LogEntry>& AllEntries()     const { return m_all; }
    const std::vector<LogEntry>& VisibleEntries() const { return m_visible; }

    std::function<void(const LogEntry&)> onRowSelected;

protected:
    wxString    OnGetItemText(long item, long col) const override;
    wxListItemAttr* OnGetItemAttr(long item) const override;

private:
    void OnItemSelected(wxListEvent& evt);
    void ShowPayload(long item);
    RowColour ComputeRowColour(const LogEntry& e) const;

    std::vector<LogEntry>     m_all;
    std::vector<LogEntry>     m_visible;
    std::optional<std::regex> m_regex;

    wxTextCtrl*         m_payloadCtrl{nullptr};
    const FilterConfig* m_filterCfg{nullptr};

    // mutable because OnGetItemAttr is const
    mutable wxListItemAttr m_attr;

    wxDECLARE_EVENT_TABLE();
};
