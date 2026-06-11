#pragma once
#include "LogEntry.h"
#include "FilterConfig.h"
#include <wx/listctrl.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/textctrl.h>
#include <vector>
#include <deque>
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

    // Positive + negative regex filters applied together
    std::size_t ApplyFilter(const std::string& posPattern,
                             const std::string& negPattern = "");
    void        ClearFilter();

    void SetFilterConfig(const FilterConfig* cfg);

    const std::vector<LogEntry>& AllEntries()     const { return m_all; }
    const std::vector<LogEntry>& VisibleEntries() const { return m_visible; }

    // Returns entries for all currently selected rows (multi-select).
    // Falls back to all visible if nothing selected.
    std::vector<LogEntry> GetSelectedEntries() const;

    // Number of selected rows
    int GetSelectedCount() const;

    std::function<void(const LogEntry&)> onRowSelected;

protected:
    wxString        OnGetItemText(long item, long col) const override;
    wxListItemAttr* OnGetItemAttr(long item) const override;

private:
    void OnItemSelected(wxListEvent& evt);
    void OnItemDeselected(wxListEvent& evt);
    void OnContextMenu(wxContextMenuEvent& evt);
    void OnCopyRows(wxCommandEvent& evt);
    void OnCopyCell(wxCommandEvent& evt);

    // Build tab-separated text from selected (or all visible) rows
    wxString SelectedRowsAsText(bool rawOnly = false) const;

    enum {
        ID_CTX_COPY_ROWS = wxID_HIGHEST + 500,
        ID_CTX_COPY_RAW,
        ID_CTX_COPY_CELL,
        ID_CTX_SELECT_ALL,
    };
    long m_contextItem{-1};  // row right-clicked
    void ShowPayload(long item);
    bool MatchesFilters(const LogEntry& e) const;
    RowColour ComputeRowColour(const LogEntry& e) const;

    std::vector<LogEntry>     m_all;
    std::vector<LogEntry>     m_visible;
    std::optional<std::regex> m_posRegex;
    std::optional<std::regex> m_negRegex;

    wxTextCtrl*         m_payloadCtrl{nullptr};
    const FilterConfig* m_filterCfg{nullptr};

    // Per-row attribute cache — one entry per visible row.
    // Rebuilt in SetEntries/ApplyFilter/AppendLine; avoids the
    // single-shared-instance bug where all rows get the last colour.
    // deque: push_back never invalidates existing element pointers.
    // This is critical because wx may hold a raw pointer (from OnGetItemAttr)
    // across the same paint cycle where AppendLine runs.
    mutable std::deque<wxListItemAttr> m_attrCache;
    void RebuildAttrCache();
    void AppendOneAttr(const LogEntry& e);

    wxDECLARE_EVENT_TABLE();
};
