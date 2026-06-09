#include "LogTableCtrl.h"
#include <wx/textctrl.h>
#include <cstdint>

wxBEGIN_EVENT_TABLE(LogTableCtrl, wxListCtrl)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, LogTableCtrl::OnItemSelected)
wxEND_EVENT_TABLE()

static wxColour fromARGB(uint32_t c)
{
    return wxColour((c>>16)&0xFF,(c>>8)&0xFF,c&0xFF,(c>>24)&0xFF);
}

LogTableCtrl::LogTableCtrl(wxWindow* parent, wxWindowID id)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT|wxLC_VIRTUAL|wxLC_SINGLE_SEL|wxLC_HRULES|wxLC_VRULES)
{
    wxListItem col;
    col.SetId(0); col.SetText("Timestamp"); col.SetWidth(200); InsertColumn(0,col);
    col.SetId(1); col.SetText("Hostname");  col.SetWidth(130); InsertColumn(1,col);
    col.SetId(2); col.SetText("Service");   col.SetWidth(130); InsertColumn(2,col);
    col.SetId(3); col.SetText("PID");       col.SetWidth(65);  InsertColumn(3,col);
    col.SetId(4); col.SetText("Message");   col.SetWidth(700); InsertColumn(4,col);
    SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
}

// ── colour computation ──────────────────────────────────────────────────────
RowColour LogTableCtrl::ComputeRowColour(const LogEntry& e) const
{
    if (!m_filterCfg) return {};
    for (const auto& rule : m_filterCfg->Rules())
    {
        if (!rule.enabled) continue;

        if (!rule.fixedPayload.empty() &&
            e.raw.find(rule.fixedPayload) == std::string::npos) continue;

        if (!rule.payloadRegex.empty())
        {
            try {
                if (!std::regex_search(e.raw,
                        std::regex(rule.payloadRegex, std::regex::icase)))
                    continue;
            } catch (...) { continue; }
        }

        if (!rule.application.empty() &&
            e.service.find(rule.application) == std::string::npos) continue;

        if (!rule.tsFrom.empty() && e.timestamp < rule.tsFrom) continue;
        if (!rule.tsTo.empty()   && e.timestamp > rule.tsTo)   continue;

        RowColour rc;
        rc.hasColour = true;
        rc.bg = fromARGB(rule.bgColour);
        rc.fg = fromARGB(rule.fgColour);
        return rc;
    }
    return {};
}

// Full rebuild of the per-row attribute deque from m_visible.
// Must be called whenever m_visible is replaced wholesale (filter/clear/set).
// For incremental appends use AppendOneAttr() instead.
void LogTableCtrl::RebuildAttrCache()
{
    m_attrCache.clear();
    for (const auto& e : m_visible)
    {
        m_attrCache.emplace_back();           // default: no colour
        RowColour rc = ComputeRowColour(e);
        if (rc.hasColour)
        {
            m_attrCache.back().SetBackgroundColour(rc.bg);
            m_attrCache.back().SetTextColour(rc.fg);
        }
    }
}

// Append one attr slot for the entry that was just pushed onto m_visible.
// deque::push_back never invalidates existing element pointers, so any
// pointer that wx is holding from a prior OnGetItemAttr call stays valid.
void LogTableCtrl::AppendOneAttr(const LogEntry& e)
{
    m_attrCache.emplace_back();
    RowColour rc = ComputeRowColour(e);
    if (rc.hasColour)
    {
        m_attrCache.back().SetBackgroundColour(rc.bg);
        m_attrCache.back().SetTextColour(rc.fg);
    }
}

// ── filter helpers ──────────────────────────────────────────────────────────
bool LogTableCtrl::MatchesFilters(const LogEntry& e) const
{
    if (m_posRegex && !std::regex_search(e.raw, *m_posRegex)) return false;
    if (m_negRegex &&  std::regex_search(e.raw, *m_negRegex)) return false;
    return true;
}

// ── public API ──────────────────────────────────────────────────────────────
void LogTableCtrl::AppendLine(const std::string& line)
{
    LogEntry e = LogEntry::Parse(line);
    m_all.push_back(e);

    if (MatchesFilters(e))
    {
        m_visible.push_back(e);
        AppendOneAttr(e);      // deque: safe, no pointer invalidation

        const long newCount = (long)m_visible.size();
        SetItemCount(newCount);
        EnsureVisible(newCount - 1);
        RefreshItem(newCount - 1);
    }
}

void LogTableCtrl::SetEntries(const std::vector<LogEntry>& entries)
{
    m_all = entries;
    m_visible.clear();
    for (const auto& e : m_all)
        if (MatchesFilters(e)) m_visible.push_back(e);

    RebuildAttrCache();
    SetItemCount((long)m_visible.size());
    Refresh();
}

void LogTableCtrl::ClearAll()
{
    m_all.clear();
    m_visible.clear();
    m_posRegex.reset();
    m_negRegex.reset();
    m_attrCache.clear();
    SetItemCount(0);
    Refresh();
    if (m_payloadCtrl) m_payloadCtrl->Clear();
}

void LogTableCtrl::SetPayload(const std::string& text)
{
    if (m_payloadCtrl) m_payloadCtrl->SetValue(wxString::FromUTF8(text));
}

std::size_t LogTableCtrl::ApplyFilter(const std::string& pos, const std::string& neg)
{
    m_posRegex.reset();
    m_negRegex.reset();

    try { if (!pos.empty()) m_posRegex = std::regex(pos, std::regex::icase); }
    catch (...) {}
    try { if (!neg.empty()) m_negRegex = std::regex(neg, std::regex::icase); }
    catch (...) {}

    m_visible.clear();
    for (const auto& e : m_all)
        if (MatchesFilters(e)) m_visible.push_back(e);

    RebuildAttrCache();
    SetItemCount((long)m_visible.size());
    Refresh();
    return m_visible.size();
}

void LogTableCtrl::ClearFilter()
{
    m_posRegex.reset();
    m_negRegex.reset();
    m_visible = m_all;
    RebuildAttrCache();
    SetItemCount((long)m_visible.size());
    Refresh();
}

void LogTableCtrl::SetFilterConfig(const FilterConfig* cfg)
{
    m_filterCfg = cfg;
    RebuildAttrCache();   // recolour existing rows
    Refresh();
}

// ── wxListCtrl virtual overrides ────────────────────────────────────────────
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

wxListItemAttr* LogTableCtrl::OnGetItemAttr(long item) const
{
    if (item < 0 || (std::size_t)item >= m_attrCache.size()) return nullptr;

    const wxListItemAttr& a = m_attrCache[(std::size_t)item];
    if (!a.HasBackgroundColour() && !a.HasTextColour()) return nullptr;

    // Each deque slot is a distinct object — returning its address is safe.
    // deque element pointers remain stable across push_back operations.
    return const_cast<wxListItemAttr*>(&m_attrCache[(std::size_t)item]);
}

// ── selection ────────────────────────────────────────────────────────────────
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
    if (!m_payloadCtrl || item < 0 || (std::size_t)item >= m_visible.size()) return;
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
