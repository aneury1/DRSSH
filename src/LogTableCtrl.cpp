#include "LogTableCtrl.h"
#include <wx/textctrl.h>
#include <regex>
#include <cstdint>

wxBEGIN_EVENT_TABLE(LogTableCtrl, wxListCtrl)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, LogTableCtrl::OnItemSelected)
wxEND_EVENT_TABLE()

static wxColour fromARGB(uint32_t c)
{
    uint8_t a = (c >> 24) & 0xFF;
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b = (c >>  0) & 0xFF;
    return wxColour(r, g, b, a);
}

LogTableCtrl::LogTableCtrl(wxWindow* parent, wxWindowID id)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES)
{
    wxListItem col;
    col.SetId(0); col.SetText("Timestamp");  col.SetWidth(200); InsertColumn(0, col);
    col.SetId(1); col.SetText("Hostname");   col.SetWidth(130); InsertColumn(1, col);
    col.SetId(2); col.SetText("Service");    col.SetWidth(130); InsertColumn(2, col);
    col.SetId(3); col.SetText("PID");        col.SetWidth(65);  InsertColumn(3, col);
    col.SetId(4); col.SetText("Message");    col.SetWidth(700); InsertColumn(4, col);
    SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
}

void LogTableCtrl::AppendLine(const std::string& line)
{
    LogEntry e = LogEntry::Parse(line);
    m_all.push_back(e);
    if (!m_regex || std::regex_search(line, *m_regex))
    {
        m_visible.push_back(e);
        SetItemCount((long)m_visible.size());
        EnsureVisible((long)m_visible.size() - 1);
        RefreshItem((long)m_visible.size() - 1);
    }
}

void LogTableCtrl::SetEntries(const std::vector<LogEntry>& entries)
{
    m_all     = entries;
    m_visible = entries;
    m_regex.reset();
    SetItemCount((long)m_visible.size());
    Refresh();
}

void LogTableCtrl::ClearAll()
{
    m_all.clear();
    m_visible.clear();
    m_regex.reset();
    SetItemCount(0);
    Refresh();
    if (m_payloadCtrl) m_payloadCtrl->Clear();
}

void LogTableCtrl::SetPayload(const std::string& text)
{
    if (m_payloadCtrl) m_payloadCtrl->SetValue(wxString::FromUTF8(text));
}

std::size_t LogTableCtrl::ApplyFilter(const std::string& pattern)
{
    if (pattern.empty()) { ClearFilter(); return m_visible.size(); }
    try
    {
        m_regex   = std::regex(pattern, std::regex::icase);
        m_visible.clear();
        for (const auto& e : m_all)
            if (std::regex_search(e.raw, *m_regex))
                m_visible.push_back(e);
    }
    catch (const std::regex_error&) { m_regex.reset(); }
    SetItemCount((long)m_visible.size());
    Refresh();
    return m_visible.size();
}

void LogTableCtrl::ClearFilter()
{
    m_regex.reset();
    m_visible = m_all;
    SetItemCount((long)m_visible.size());
    Refresh();
}

wxString LogTableCtrl::OnGetItemText(long item, long col) const
{
    if (item < 0 || (std::size_t)item >= m_visible.size()) return {};
    const auto& e = m_visible[(std::size_t)item];
    switch (col)
    {
        case 0: return wxString::FromUTF8(e.timestamp);
        case 1: return wxString::FromUTF8(e.hostname);
        case 2: return wxString::FromUTF8(e.service);
        case 3: return wxString::FromUTF8(e.pid);
        case 4: return wxString::FromUTF8(e.message);
        default: return {};
    }
}

RowColour LogTableCtrl::ComputeRowColour(const LogEntry& e) const
{
    if (!m_filterCfg) return {};
    for (const auto& rule : m_filterCfg->Rules())
    {
        if (!rule.enabled) continue;

        // Fixed payload substring
        if (!rule.fixedPayload.empty() &&
            e.raw.find(rule.fixedPayload) == std::string::npos)
            continue;

        // Payload regex
        if (!rule.payloadRegex.empty())
        {
            try {
                if (!std::regex_search(e.raw, std::regex(rule.payloadRegex,
                                                          std::regex::icase)))
                    continue;
            } catch (...) { continue; }
        }

        // Application (service field)
        if (!rule.application.empty() &&
            e.service.find(rule.application) == std::string::npos)
            continue;

        // Timestamp range
        if (!rule.tsFrom.empty() && e.timestamp < rule.tsFrom) continue;
        if (!rule.tsTo.empty()   && e.timestamp > rule.tsTo)   continue;

        // All criteria match
        RowColour rc;
        rc.hasColour = true;
        rc.bg = fromARGB(rule.bgColour);
        rc.fg = fromARGB(rule.fgColour);
        return rc;
    }
    return {};
}

wxListItemAttr* LogTableCtrl::OnGetItemAttr(long item) const
{
    if (item < 0 || (std::size_t)item >= m_visible.size()) return nullptr;
    RowColour rc = ComputeRowColour(m_visible[(std::size_t)item]);
    if (!rc.hasColour) return nullptr;
    m_attr.SetBackgroundColour(rc.bg);
    m_attr.SetTextColour(rc.fg);
    return &m_attr;
}

void LogTableCtrl::OnItemSelected(wxListEvent& evt)
{
    ShowPayload(evt.GetIndex());
    if (onRowSelected)
    {
        long idx = evt.GetIndex();
        if (idx >= 0 && (std::size_t)idx < m_visible.size())
            onRowSelected(m_visible[(std::size_t)idx]);
    }
}

void LogTableCtrl::ShowPayload(long item)
{
    if (!m_payloadCtrl) return;
    if (item < 0 || (std::size_t)item >= m_visible.size()) return;
    const LogEntry& e = m_visible[(std::size_t)item];
    wxString p;
    p << "--- Raw ---\n" << wxString::FromUTF8(e.raw) << "\n\n";
    p << "Timestamp : " << wxString::FromUTF8(e.timestamp) << "\n";
    p << "Hostname  : " << wxString::FromUTF8(e.hostname)  << "\n";
    p << "Service   : " << wxString::FromUTF8(e.service)   << "\n";
    p << "PID       : " << wxString::FromUTF8(e.pid)       << "\n";
    p << "Message   : " << wxString::FromUTF8(e.message)   << "\n";
    m_payloadCtrl->SetValue(p);
}
