#include "LogTableCtrl.h"
#include <wx/textctrl.h>
#include <cstdint>

wxBEGIN_EVENT_TABLE(LogTableCtrl, wxListCtrl)
    EVT_LIST_ITEM_SELECTED(wxID_ANY,   LogTableCtrl::OnItemSelected)
    EVT_LIST_ITEM_DESELECTED(wxID_ANY, LogTableCtrl::OnItemDeselected)
    EVT_CONTEXT_MENU(                  LogTableCtrl::OnContextMenu)
    EVT_MENU(ID_CTX_COPY_ROWS,         LogTableCtrl::OnCopyRows)
    EVT_MENU(ID_CTX_COPY_RAW,          LogTableCtrl::OnCopyRows)
    EVT_MENU(ID_CTX_COPY_CELL,         LogTableCtrl::OnCopyCell)
    EVT_MENU(ID_CTX_SELECT_ALL,        LogTableCtrl::OnCopyRows)
wxEND_EVENT_TABLE()

static wxColour fromARGB(uint32_t c)
{
    return wxColour((c>>16)&0xFF,(c>>8)&0xFF,c&0xFF,(c>>24)&0xFF);
}

LogTableCtrl::~LogTableCtrl()
{
    // wxLC_VIRTUAL calls OnGetItemText/OnGetItemAttr during base-class
    // destruction (EVT_LIST_DELETE_ALL_ITEMS, internal repaint, etc.).
    // Set item count to 0 first so the base class never calls our overrides
    // with a non-zero item count, then clear our data so any stray call
    // gets an empty range and returns immediately.
    SetItemCount(0);
    m_all.clear();
    m_visible.clear();
    m_attrCache.clear();
    m_posRegex.reset();
    m_negRegex.reset();
    onRowSelected = nullptr;
    m_payloadCtrl = nullptr;   // not owned by us; just stop using it
    m_filterCfg   = nullptr;
}

LogTableCtrl::LogTableCtrl(wxWindow* parent, wxWindowID id)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT|wxLC_VIRTUAL|wxLC_HRULES|wxLC_VRULES)
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
std::vector<LogEntry> LogTableCtrl::GetSelectedEntries() const
{
    std::vector<LogEntry> result;
    long item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    while (item != wxNOT_FOUND)
    {
        if ((std::size_t)item < m_visible.size())
            result.push_back(m_visible[(std::size_t)item]);
        item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    }
    return result;
}

int LogTableCtrl::GetSelectedCount() const
{
    return GetSelectedItemCount();
}

void LogTableCtrl::OnItemDeselected(wxListEvent&)
{
    // Update status — payload keeps last selected row content
}

void LogTableCtrl::OnItemSelected(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    // Always show the payload for the focused (last-clicked) row
    ShowPayload(idx);

    if (onRowSelected && idx >= 0 && (std::size_t)idx < m_visible.size())
        onRowSelected(m_visible[(std::size_t)idx]);
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

// ── Context menu ─────────────────────────────────────────────────────────────
void LogTableCtrl::OnContextMenu(wxContextMenuEvent& evt)
{
    // Record which row was right-clicked (may be -1 if on empty area)
    wxPoint pt = ScreenToClient(evt.GetPosition());
    int flags = 0;
    m_contextItem = HitTest(pt, flags);

    int selCount = GetSelectedItemCount();

    wxMenu menu;

    if (selCount > 1)
        menu.Append(ID_CTX_COPY_ROWS, wxString::Format("Copy %d selected rows (TSV)", selCount));
    else
        menu.Append(ID_CTX_COPY_ROWS, "Copy row (TSV)");

    menu.Append(ID_CTX_COPY_RAW,  selCount > 1
                                   ? wxString::Format("Copy %d raw lines", selCount)
                                   : wxString("Copy raw line"));

    if (m_contextItem >= 0)
    {
        menu.AppendSeparator();
        menu.Append(ID_CTX_COPY_CELL, "Copy cell under cursor");
    }

    menu.AppendSeparator();
    menu.Append(ID_CTX_SELECT_ALL, "Select all visible rows");

    PopupMenu(&menu);
}

wxString LogTableCtrl::SelectedRowsAsText(bool rawOnly) const
{
    // Collect selected rows; if none selected use the context-menu row;
    // if that is also -1, use all visible rows.
    std::vector<LogEntry> rows = GetSelectedEntries();

    if (rows.empty() && m_contextItem >= 0 &&
        (std::size_t)m_contextItem < m_visible.size())
        rows.push_back(m_visible[(std::size_t)m_contextItem]);

    if (rows.empty())
        rows = m_visible;   // fallback: all visible

    if (rows.empty()) return {};

    wxString text;

    if (rawOnly)
    {
        for (const auto& e : rows)
            text << wxString::FromUTF8(e.raw) << "\n";
        return text;
    }

    // TSV header
    text << "Timestamp\tHostname\tService\tPID\tMessage\n";

    for (const auto& e : rows)
    {
        auto clean = [](const std::string& s) -> wxString {
            wxString w = wxString::FromUTF8(s);
            w.Replace("\t", " ");   // no tabs inside cells
            return w;
        };
        text << clean(e.timestamp) << "\t"
             << clean(e.hostname)  << "\t"
             << clean(e.service)   << "\t"
             << clean(e.pid)       << "\t"
             << clean(e.message)   << "\n";
    }
    return text;
}

void LogTableCtrl::OnCopyRows(wxCommandEvent& evt)
{
    if (evt.GetId() == ID_CTX_SELECT_ALL)
    {
        // Select all visible items
        for (long i = 0; i < (long)m_visible.size(); ++i)
            SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        return;
    }

    bool rawOnly = (evt.GetId() == ID_CTX_COPY_RAW);
    wxString text = SelectedRowsAsText(rawOnly);
    if (text.IsEmpty()) return;

    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
}

void LogTableCtrl::OnCopyCell(wxCommandEvent&)
{
    if (m_contextItem < 0 || (std::size_t)m_contextItem >= m_visible.size()) return;

    // Figure out which column was clicked by comparing cursor X to column widths
    wxPoint cursorLocal = ScreenToClient(wxGetMousePosition());
    int flags = 0;
    HitTest(cursorLocal, flags);   // updates flags but not column directly

    // Walk columns to find which one contains the X coordinate
    int x = cursorLocal.x;
    // GetScrollPos gives horizontal scroll offset
    x += GetScrollPos(wxHORIZONTAL);

    int colStart = 0;
    int clickedCol = 4;   // default to message
    for (int c = 0; c < GetColumnCount(); ++c)
    {
        int w = GetColumnWidth(c);
        if (x >= colStart && x < colStart + w)
        {
            clickedCol = c;
            break;
        }
        colStart += w;
    }

    const LogEntry& e = m_visible[(std::size_t)m_contextItem];
    wxString cell;
    switch (clickedCol)
    {
        case 0: cell = wxString::FromUTF8(e.timestamp); break;
        case 1: cell = wxString::FromUTF8(e.hostname);  break;
        case 2: cell = wxString::FromUTF8(e.service);   break;
        case 3: cell = wxString::FromUTF8(e.pid);       break;
        default: cell = wxString::FromUTF8(e.message);  break;
    }

    if (!cell.IsEmpty() && wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(cell));
        wxTheClipboard->Close();
    }
}
