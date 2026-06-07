#include "LogTableCtrl.h"
#include "LogEntry.h"
#include <wx/textctrl.h>

wxBEGIN_EVENT_TABLE(LogTableCtrl, wxListCtrl)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, LogTableCtrl::OnItemSelected)
wxEND_EVENT_TABLE()

LogTableCtrl::LogTableCtrl(wxWindow* parent, wxWindowID id)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES)
{
    wxListItem col;

    col.SetId(0); col.SetText("Timestamp");  col.SetWidth(200); InsertColumn(0, col);
    col.SetId(1); col.SetText("Hostname");   col.SetWidth(140); InsertColumn(1, col);
    col.SetId(2); col.SetText("Service");    col.SetWidth(140); InsertColumn(2, col);
    col.SetId(3); col.SetText("PID");        col.SetWidth(70);  InsertColumn(3, col);
    col.SetId(4); col.SetText("Message");    col.SetWidth(600); InsertColumn(4, col);

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

    if (m_payloadCtrl)
        m_payloadCtrl->Clear();
}

void LogTableCtrl::SetPayload(const std::string& text)
{
    if (m_payloadCtrl)
        m_payloadCtrl->SetValue(wxString::FromUTF8(text));
}

std::size_t LogTableCtrl::ApplyFilter(const std::string& pattern)
{
    if (pattern.empty())
    {
        ClearFilter();
        return m_visible.size();
    }

    try
    {
        m_regex   = std::regex(pattern, std::regex::icase);
        m_visible.clear();

        for (const auto& e : m_all)
            if (std::regex_search(e.raw, *m_regex))
                m_visible.push_back(e);
    }
    catch (const std::regex_error&)
    {
        m_regex.reset();
    }

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
    if (item < 0 || (std::size_t)item >= m_visible.size())
        return {};

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

    wxString payload;
    payload << "--- Raw ---\n" << wxString::FromUTF8(e.raw) << "\n\n";
    payload << "Timestamp : " << wxString::FromUTF8(e.timestamp) << "\n";
    payload << "Hostname  : " << wxString::FromUTF8(e.hostname)  << "\n";
    payload << "Service   : " << wxString::FromUTF8(e.service)   << "\n";
    payload << "PID       : " << wxString::FromUTF8(e.pid)       << "\n";
    payload << "Message   : " << wxString::FromUTF8(e.message)   << "\n";

    m_payloadCtrl->SetValue(payload);
}
