#include <wx/wx.h>
#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/splitter.h>
#include <wx/datetime.h>
#include <wx/stdpaths.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/clrpicker.h>

#include "MainFrame.h"
#include "Events.h"
#include "LogEntry.h"
#include "Database.h"

#include <fstream>
#include <sstream>
#include <regex>

wxDialog* CreateProfileDialog(wxWindow* parent, ProfileManager& mgr);
wxDialog* CreateFilterConfigDialog(wxWindow* parent, FilterConfig& cfg);

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(JournalApp);

// ─────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Remote Journalctl Viewer",
              wxDefaultPosition, wxSize(1400, 860))
{
    SetMinSize(wxSize(900, 600));
    BuildMenu();
    CreateControls();
    Bind(EVT_SSH_LOG,    &MainFrame::OnSSHLog,   this);
    Bind(EVT_SSH_STATUS, &MainFrame::OnSSHStatus, this);
    CreateStatusBar(2);
    SetStatusText("Ready");
    LoadSettings();
}

MainFrame::~MainFrame()
{
    for (auto& tab : m_tabs)
        if (tab->reader)
        {
            tab->reader->SetCallback(nullptr);
            tab->reader->Stop();
        }
}

// ─────────────────────────────────────────────
void MainFrame::BuildMenu()
{
    m_menuBar = new wxMenuBar;

    // File
    auto* file = new wxMenu;
    file->Append(ID_MENU_NEW_TAB,   "&New Tab\tCtrl+T");
    file->Append(ID_MENU_CLOSE_TAB, "&Close Tab\tCtrl+W");
    file->AppendSeparator();
    file->Append(ID_MENU_SAVE,      "&Save Logs (text)...\tCtrl+S");
    file->Append(ID_MENU_SAVE_DB,   "Save Logs to &SQLite...");
    file->Append(ID_MENU_OPEN_DB,   "&Open SQLite DB...\tCtrl+O");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    m_menuBar->Append(file, "&File");

    // Connection
    auto* conn = new wxMenu;
    conn->Append(ID_MENU_CONNECT,         "&Connect\tCtrl+K");
    conn->Append(ID_MENU_DISCONNECT,      "&Disconnect\tCtrl+D");
    conn->AppendSeparator();
    conn->Append(ID_MENU_MANAGE_PROFILES, "&Manage Profiles...");
    conn->AppendSeparator();
    m_profileMenu = new wxMenu;
    conn->AppendSubMenu(m_profileMenu, "Load &Profile");
    m_menuBar->Append(conn, "&Connection");

    // Tools
    auto* tools = new wxMenu;
    tools->Append(ID_MENU_APPLY_FILTER,    "Apply &Filter\tCtrl+F");
    tools->Append(ID_MENU_CLEAR_FILTER,    "Clear Filter");
    tools->AppendSeparator();
    tools->Append(ID_MENU_UPLOAD_FILE,     "&Upload File...");
    tools->Append(ID_MENU_EXECUTE_COMMAND, "Run &Command...\tCtrl+R");
    m_menuBar->Append(tools, "&Tools");

    // Filters menu (new)
    auto* filters = new wxMenu;
    filters->Append(ID_MENU_FILTER_CONFIG,    "&Configure Filters...\tCtrl+Shift+F");
    filters->AppendSeparator();
    filters->Append(ID_MENU_LOAD_FILTER_JSON, "&Load Filter JSON...");
    filters->Append(ID_MENU_SAVE_FILTER_JSON, "&Save Filter JSON...");
    m_menuBar->Append(filters, "F&ilters");

    // View
    auto* view = new wxMenu;
    view->AppendCheckItem(ID_MENU_DARK_THEME, "&Dark Theme\tCtrl+Shift+D");
    m_menuBar->Append(view, "&View");

    SetMenuBar(m_menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnMenuNewTab,         this, ID_MENU_NEW_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuCloseTab,       this, ID_MENU_CLOSE_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSave,           this, ID_MENU_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSaveDb,         this, ID_MENU_SAVE_DB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuOpenDb,         this, ID_MENU_OPEN_DB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuConnect,        this, ID_MENU_CONNECT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuDisconnect,     this, ID_MENU_DISCONNECT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuManageProfiles, this, ID_MENU_MANAGE_PROFILES);
    Bind(wxEVT_MENU, &MainFrame::OnMenuApplyFilter,    this, ID_MENU_APPLY_FILTER);
    Bind(wxEVT_MENU, &MainFrame::OnMenuClearFilter,    this, ID_MENU_CLEAR_FILTER);
    Bind(wxEVT_MENU, &MainFrame::OnMenuUploadFile,     this, ID_MENU_UPLOAD_FILE);
    Bind(wxEVT_MENU, &MainFrame::OnMenuExecuteCommand, this, ID_MENU_EXECUTE_COMMAND);
    Bind(wxEVT_MENU, &MainFrame::OnMenuDarkTheme,      this, ID_MENU_DARK_THEME);
    Bind(wxEVT_MENU, &MainFrame::OnMenuFilterConfig,   this, ID_MENU_FILTER_CONFIG);
    Bind(wxEVT_MENU, &MainFrame::OnMenuLoadFilterJson, this, ID_MENU_LOAD_FILTER_JSON);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSaveFilterJson, this, ID_MENU_SAVE_FILTER_JSON);
}

void MainFrame::BuildToolBar() {}

void MainFrame::CreateControls()
{
    m_notebook = new wxNotebook(this, wxID_ANY);
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_notebook, 1, wxEXPAND | wxALL, 4);
    SetSizer(root);
}

// ─────────────────────────────────────────────
ConnectionTab& MainFrame::AddConnectionTab(const wxString& title, bool select)
{
    auto tab = std::make_unique<ConnectionTab>();
    tab->page = new wxPanel(m_notebook, wxID_ANY);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // 2-col grid: label | control-row
    auto* connBox = new wxStaticBoxSizer(wxVERTICAL, tab->page, "Connection");
    auto* grid    = new wxFlexGridSizer(2, 6, 8);   // 2 cols, vgap=6, hgap=8
    grid->AddGrowableCol(1);

    auto addLabel = [&](const wxString& lbl) {
        grid->Add(new wxStaticText(tab->page,wxID_ANY,lbl), 0, wxALIGN_CENTER_VERTICAL);
    };
    auto addField = [&](const wxString& lbl, wxTextCtrl*& ctrl, long style=0) {
        addLabel(lbl);
        ctrl = new wxTextCtrl(tab->page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, style);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    // Row 1: Host + Port + Connect/Disconnect
    addLabel("Host:");
    {
        auto* hostRow = new wxBoxSizer(wxHORIZONTAL);
        tab->host = new wxTextCtrl(tab->page, wxID_ANY);
        tab->host->SetHint("192.168.1.1");
        tab->port = new wxSpinCtrl(tab->page, wxID_ANY, "22",
                                    wxDefaultPosition, wxSize(70,-1),
                                    wxSP_ARROW_KEYS, 1, 65535, 22);
        auto* btnConn  = new wxButton(tab->page, wxID_ANY, "Connect",
                                      wxDefaultPosition, wxSize(90,-1));
        auto* btnDisco = new wxButton(tab->page, wxID_ANY, "Disconnect",
                                      wxDefaultPosition, wxSize(90,-1));
        hostRow->Add(tab->host,  1, wxEXPAND | wxRIGHT, 6);
        hostRow->Add(new wxStaticText(tab->page,wxID_ANY,"Port:"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        hostRow->Add(tab->port,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        hostRow->Add(btnConn,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        hostRow->Add(btnDisco,   0, wxALIGN_CENTER_VERTICAL);
        grid->Add(hostRow, 1, wxEXPAND);

        btnConn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            auto* t = CurrentTab(); if (!t) return;
            SetStatusText("Connecting...");
            SaveSettings();
            DoConnect(*t, CurrentTabIndex());
        });
        btnDisco->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            auto* t = CurrentTab(); if (t) DoDisconnect(*t);
            SetStatusText("Disconnected");
        });
    }

    // Row 2: User
    addField("User:", tab->user); tab->user->SetHint("root");

    // Row 3: Password
    addField("Password:", tab->password, wxTE_PASSWORD);

    // Row 4: Filter + Apply / Clear / Save Lines
    addLabel("Filter:");
    {
        auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
        tab->filter = new wxTextCtrl(tab->page, wxID_ANY);
        tab->filter->SetHint("regex: error|warn|sshd");
        auto* btnApply = new wxButton(tab->page, wxID_ANY, "Apply",
                                      wxDefaultPosition, wxSize(70,-1));
        auto* btnClear = new wxButton(tab->page, wxID_ANY, "Clear",
                                      wxDefaultPosition, wxSize(60,-1));
        auto* btnSaveF = new wxButton(tab->page, wxID_ANY, "Save Lines",
                                      wxDefaultPosition, wxSize(90,-1));
        filterRow->Add(tab->filter, 1, wxEXPAND | wxRIGHT, 6);
        filterRow->Add(btnApply,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        filterRow->Add(btnClear,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        filterRow->Add(btnSaveF,    0, wxALIGN_CENTER_VERTICAL);
        grid->Add(filterRow, 1, wxEXPAND);

        btnApply->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e){ OnMenuApplyFilter(e); });
        btnClear->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e){ OnMenuClearFilter(e); });
        btnSaveF->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            auto* t = CurrentTab(); if (!t) return;
            const auto& vis = t->logTable->VisibleEntries();
            if (vis.empty()) { SetStatusText("No filtered lines to save"); return; }
            wxDateTime now = wxDateTime::Now();
            wxString host = t->host->GetValue(); host.Replace(".", "_");
            wxString def = now.Format("%Y%m%d_%H%M%S_") + host + "_filtered.txt";
            wxFileDialog dlg(this,"Save Filtered Lines","",def,
                             "Text files (*.txt)|*.txt|All (*)|*",
                             wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal()!=wxID_OK) return;
            std::ofstream out(dlg.GetPath().ToStdString());
            for (const auto& e : vis) out << e.raw << '\n';
            SetStatusText(wxString::Format("Saved %zu filtered lines", vis.size()));
        });
    }

    // Row 5: jctl args
    addField("jctl args:", tab->journalArgs);
    tab->journalArgs->SetHint("-f -o short-iso");

    // Row 6: SQLite
    addLabel("SQLite:");
    {
        auto* sqlRow = new wxBoxSizer(wxHORIZONTAL);
        tab->useSqlite = new wxCheckBox(tab->page, wxID_ANY, "Enable");
        tab->dbPath    = new wxTextCtrl(tab->page, wxID_ANY);
        tab->dbPath->SetHint("auto: YYYYMMDD_host.db");
        sqlRow->Add(tab->useSqlite, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        sqlRow->Add(tab->dbPath,    1, wxEXPAND);
        grid->Add(sqlRow, 1, wxEXPAND);
    }

    connBox->Add(grid, 0, wxEXPAND | wxALL, 8);
    root->Add(connBox, 0, wxEXPAND | wxALL, 4);

    // Splitter: table | payload
    auto* splitter = new wxSplitterWindow(tab->page, wxID_ANY,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxSP_LIVE_UPDATE | wxSP_3D);
    tab->logTable    = new LogTableCtrl(splitter);
    tab->payloadCtrl = new wxTextCtrl(splitter, wxID_ANY, "",
                                       wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE|wxTE_READONLY|wxTE_RICH2);
    tab->payloadCtrl->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    tab->logTable->SetPayloadCtrl(tab->payloadCtrl);
    tab->logTable->SetFilterConfig(&m_filterConfig);

    splitter->SplitHorizontally(tab->logTable, tab->payloadCtrl, -180);
    splitter->SetMinimumPaneSize(60);
    root->Add(splitter, 1, wxEXPAND | wxALL, 4);

    tab->page->SetSizer(root);
    tab->reader = std::make_unique<SSHJournalReader>();

    auto& ref = *tab;
    m_tabs.push_back(std::move(tab));
    m_notebook->AddPage(ref.page, title, select);
    return ref;
}

void MainFrame::UpdateTabTitle(std::size_t index)
{
    auto* t = TabAt(index);
    if (!t) return;
    wxString label = t->host->GetValue();
    if (label.IsEmpty()) label = wxString::Format("Tab %zu", index+1);
    m_notebook->SetPageText((int)index, label);
}

void MainFrame::CloseConnectionTab(std::size_t index)
{
    if (m_tabs.size()==1) { SetStatusText("Cannot close the last tab"); return; }
    auto* t = TabAt(index);
    if (!t) return;
    if (t->reader) { t->reader->SetCallback(nullptr); t->reader->Stop(); }
    if (t->db)     { t->db->Close(); }
    m_notebook->DeletePage((int)index);
    m_tabs.erase(m_tabs.begin()+(std::ptrdiff_t)index);
    std::size_t next = (index>=m_tabs.size()) ? m_tabs.size()-1 : index;
    if (!m_tabs.empty()) m_notebook->SetSelection((int)next);
    SaveSettings();
}

ConnectionTab* MainFrame::CurrentTab()  { return TabAt(CurrentTabIndex()); }
ConnectionTab* MainFrame::TabAt(std::size_t i)
{
    return (i<m_tabs.size()) ? m_tabs[i].get() : nullptr;
}
std::size_t MainFrame::CurrentTabIndex() const
{
    if (!m_notebook || m_notebook->GetPageCount()==0) return 0;
    int s = m_notebook->GetSelection();
    return (s==wxNOT_FOUND) ? 0 : (std::size_t)s;
}

// ─────────────────────────────────────────────
void MainFrame::AppendLog(std::size_t tabIndex, const std::string& line)
{
    auto* tab = TabAt(tabIndex);
    if (!tab) return;

    // SQLite insert – only when db is open and session is set
    if (tab->db && tab->db->IsOpen() && !tab->sessionId.empty())
        tab->db->InsertLog(tab->sessionId, LogEntry::Parse(line));

    tab->logTable->AppendLine(line);
}

void MainFrame::RefreshFilteredLogs(ConnectionTab& tab)
{
    auto all = tab.logTable->AllEntries();
    tab.logTable->SetEntries(all);
    if (!tab.activeFilter.empty())
        tab.logTable->ApplyFilter(tab.activeFilter);
}

void MainFrame::ApplyFilterConfigToAllTabs()
{
    for (auto& t : m_tabs)
        if (t->logTable)
        {
            t->logTable->SetFilterConfig(&m_filterConfig);
            t->logTable->Refresh();
        }
}

// ─────────────────────────────────────────────
void MainFrame::RebuildProfileMenu()
{
    while (m_profileMenu->GetMenuItemCount()>0)
        m_profileMenu->Destroy(m_profileMenu->FindItemByPosition(0));

    const auto& profs = m_profiles.Profiles();
    if (profs.empty())
    {
        m_profileMenu->Append(ID_MENU_LOAD_PROFILE_BASE, "(no profiles)");
        return;
    }
    for (std::size_t i=0; i<profs.size() && i<50; ++i)
    {
        int id = ID_MENU_LOAD_PROFILE_BASE+(int)i;
        wxString label = wxString::FromUTF8(profs[i].name.empty()?profs[i].host:profs[i].name);
        m_profileMenu->Append(id, label);
        Bind(wxEVT_MENU, &MainFrame::OnMenuLoadProfile, this, id);
    }
}

// ─────────────────────────────────────────────
void MainFrame::LoadSettings()
{
    m_profiles.Load();
    RebuildProfileMenu();

    // Try loading default filter JSON
    m_filterConfig.LoadFromFile("filters.json");

    wxConfig cfg("RemoteJournal");
    long count=0;
    cfg.Read("tabs/count",&count,0);

    if (count<=0)
    {
        AddConnectionTab("Connection 1", true);
    }
    else
    {
        for (long i=0; i<count; ++i)
        {
            auto& tab = AddConnectionTab(wxString::Format("Connection %ld",i+1), i==0);
            wxString base = wxString::Format("tabs/%ld/",i);
            wxString tmp; long port=22;
            cfg.Read(base+"host",&tmp,"");        tab.host->SetValue(tmp);
            cfg.Read(base+"port",&port,22);       tab.port->SetValue((int)port);
            cfg.Read(base+"user",&tmp,"");        tab.user->SetValue(tmp);
            cfg.Read(base+"filter",&tmp,"");      tab.filter->SetValue(tmp);
            cfg.Read(base+"journalArgs",&tmp,""); tab.journalArgs->SetValue(tmp);
            bool useSql=false;
            cfg.Read(base+"useSqlite",&useSql,false); tab.useSqlite->SetValue(useSql);
            cfg.Read(base+"dbPath",&tmp,"");      tab.dbPath->SetValue(tmp);
            UpdateTabTitle((std::size_t)i);
        }
    }

    long sel=0;
    cfg.Read("tabs/selected",&sel,0);
    if (sel>=0 && sel<(long)m_tabs.size()) m_notebook->SetSelection((int)sel);

    bool dark=false;
    cfg.Read("ui/dark",&dark,false);
    m_darkEnabled=dark;
    if (auto* mi = m_menuBar->FindItem(ID_MENU_DARK_THEME)) mi->Check(m_darkEnabled);
    ApplyTheme();

    bool max=false;
    cfg.Read("window/maximized",&max,false);
    long x=50,y=50,w=1400,h=860;
    bool hasGeo = cfg.Read("window/x",&x) && cfg.Read("window/y",&y) &&
                  cfg.Read("window/w",&w) && cfg.Read("window/h",&h);
    if (hasGeo) SetSize((int)x,(int)y,(int)w,(int)h);
    if (max) Maximize();
}

void MainFrame::SaveSettings()
{
    wxConfig cfg("RemoteJournal");
    cfg.DeleteGroup("tabs");
    cfg.Write("tabs/count",(long)m_tabs.size());
    cfg.Write("tabs/selected",(long)CurrentTabIndex());

    for (std::size_t i=0; i<m_tabs.size(); ++i)
    {
        const auto& t = *m_tabs[i];
        wxString base = wxString::Format("tabs/%zu/",i);
        cfg.Write(base+"host",        t.host->GetValue());
        cfg.Write(base+"port",        (long)t.port->GetValue());
        cfg.Write(base+"user",        t.user->GetValue());
        cfg.Write(base+"filter",      t.filter->GetValue());
        cfg.Write(base+"journalArgs", t.journalArgs->GetValue());
        cfg.Write(base+"useSqlite",   t.useSqlite->GetValue());
        cfg.Write(base+"dbPath",      t.dbPath->GetValue());
    }

    cfg.Write("ui/dark", m_darkEnabled);
    cfg.Write("window/maximized", IsMaximized());
    if (!IsMaximized())
    {
        wxPoint p=GetPosition(); wxSize s=GetSize();
        cfg.Write("window/x",(long)p.x); cfg.Write("window/y",(long)p.y);
        cfg.Write("window/w",(long)s.x); cfg.Write("window/h",(long)s.y);
    }
    cfg.Flush();
}

// ─────────────────────────────────────────────
bool MainFrame::DoConnect(ConnectionTab& tab, std::size_t tabIndex)
{
    if (tab.reader->IsRunning()) DoDisconnect(tab);

    // ── Open SQLite if requested ──
    if (tab.useSqlite->GetValue())
    {
        // Close any old db first
        if (tab.db) tab.db->Close();

        wxString path = tab.dbPath->GetValue().Trim();
        if (path.IsEmpty())
        {
            wxDateTime now = wxDateTime::Now();
            wxString host  = tab.host->GetValue();
            host.Replace(".", "_");
            path = now.Format("%Y%m%d_%H%M%S_") + host + ".db";
            tab.dbPath->SetValue(path);  // show the auto-generated name
        }

        tab.db = std::make_unique<Database>();
        if (!tab.db->Open(path.ToStdString()))
        {
            wxMessageBox("Cannot open SQLite DB:\n" + path,
                         "DB Error", wxOK|wxICON_ERROR, this);
            tab.db.reset();
            // Continue connecting even without DB
        }
        else
        {
            // Build session id: timestamp_host
            wxDateTime now = wxDateTime::Now();
            tab.sessionId  = now.Format("%Y%m%d%H%M%S").ToStdString()
                           + "_" + tab.host->GetValue().ToStdString();
        }
    }

    // ── SSH callback (queues to main thread) ──
    tab.reader->SetCallback(
        [this, tabIndex](const std::string& line)
        {
            auto* evt = new wxThreadEvent(EVT_SSH_LOG);
            evt->SetInt((int)tabIndex);
            evt->SetString(wxString::FromUTF8(line));
            wxQueueEvent(this, evt);
        });

    std::string jargs = tab.journalArgs->GetValue().ToStdString();
    if (jargs.empty()) jargs = "-f -o short-iso";
    std::string cmd = "journalctl " + jargs;

    bool ok = tab.reader->Connect(
        tab.host->GetValue().ToStdString(),
        tab.port->GetValue(),
        tab.user->GetValue().ToStdString(),
        tab.password->GetValue().ToStdString());

    if (!ok)
    {
        tab.reader->SetCallback(nullptr);
        wxMessageBox("SSH connection failed.\nCheck host / port / credentials.",
                     "Connect", wxOK|wxICON_ERROR, this);
        return false;
    }

    if (!tab.reader->Start(cmd))
    {
        tab.reader->SetCallback(nullptr);
        tab.reader->Stop();
        wxMessageBox("Connected but journalctl failed to start.",
                     "Connect", wxOK|wxICON_ERROR, this);
        return false;
    }

    UpdateTabTitle(tabIndex);

    auto* se = new wxThreadEvent(EVT_SSH_STATUS);
    se->SetInt((int)tabIndex);
    se->SetString("connected");
    wxQueueEvent(this, se);
    return true;
}

void MainFrame::DoDisconnect(ConnectionTab& tab)
{
    tab.reader->SetCallback(nullptr);
    tab.reader->Stop();
    if (tab.db) tab.db->Close();
}

void MainFrame::SaveLogsToFile(ConnectionTab& tab)
{
    const auto& entries = tab.logTable->AllEntries();
    if (entries.empty()) { SetStatusText("No logs to save"); return; }

    wxDateTime now = wxDateTime::Now();
    wxString host  = tab.host->GetValue(); host.Replace(".", "_");
    wxString defName = now.Format("%Y%m%d_%H%M%S_") + host + ".txt";

    wxFileDialog dlg(this,"Save Logs","",defName,
                     "Text files (*.txt)|*.txt|All files (*)|*",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;

    std::ofstream out(dlg.GetPath().ToStdString());
    for (const auto& e : entries) out << e.raw << '\n';
    SetStatusText("Saved: " + dlg.GetPath());
}

// ─────────────────────────────────────────────
void MainFrame::OpenDatabaseInTab(const wxString& path)
{
    // Open the DB read-only by just querying it
    auto db = std::make_unique<Database>();
    if (!db->Open(path.ToStdString()))
    {
        wxMessageBox("Cannot open database:\n" + path,
                     "Open DB", wxOK|wxICON_ERROR, this);
        return;
    }

    // Get sessions to show in title
    auto sessions = db->QuerySessions();
    wxString tabTitle = wxFileName(path).GetFullName();

    auto& tab = AddConnectionTab(tabTitle, true);
    // Fill host field with the file name for reference
    tab.host->SetValue(path);
    tab.host->SetEditable(false);
    tab.port->Disable();
    tab.user->Disable();
    tab.password->Disable();
    tab.useSqlite->Disable();
    tab.dbPath->Disable();

    // Load all rows
    std::vector<LogEntry> allEntries = db->QueryAllLogs();
    tab.logTable->SetEntries(allEntries);
    tab.logTable->SetFilterConfig(&m_filterConfig);

    // Keep db open so user can export from it via menu
    tab.db        = std::move(db);
    tab.sessionId = "";   // empty = all sessions for ExportText

    ApplyTheme();
    SetStatusText(wxString::Format("Loaded %zu rows from %s",
                                   allEntries.size(), path));
}

// ─────────────────────────────────────────────
void MainFrame::ApplyTheme()
{
    if (m_darkEnabled) ApplyDarkTheme(this);
    else               ApplyLightTheme(this);
    Refresh(); Layout();
}

void MainFrame::ApplyDarkTheme(wxWindow* w)
{
    static const wxColour bg(30,31,34), fg(220,220,220), field(20,21,24);
    if (dynamic_cast<wxTextCtrl*>(w) || dynamic_cast<wxListCtrl*>(w))
    { w->SetBackgroundColour(field); w->SetForegroundColour(fg); }
    else
    { w->SetBackgroundColour(bg); w->SetForegroundColour(fg); }
    for (auto* c : w->GetChildren()) ApplyDarkTheme(c);
}

void MainFrame::ApplyLightTheme(wxWindow* w)
{
    w->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    w->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    for (auto* c : w->GetChildren()) ApplyLightTheme(c);
}

// ─────────────────────────────────────────────
// Menu handlers
// ─────────────────────────────────────────────
void MainFrame::OnMenuNewTab(wxCommandEvent&)
{
    AddConnectionTab(wxString::Format("Connection %zu", m_tabs.size()+1), true);
    ApplyTheme(); SaveSettings();
}
void MainFrame::OnMenuCloseTab(wxCommandEvent&) { CloseConnectionTab(CurrentTabIndex()); }
void MainFrame::OnMenuSave(wxCommandEvent&) { auto* t=CurrentTab(); if(t) SaveLogsToFile(*t); }

void MainFrame::OnMenuSaveDb(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab) return;

    if (!tab->db || !tab->db->IsOpen())
    {
        wxMessageBox("SQLite logging is not enabled for this tab.\n"
                     "Enable 'SQLite > Enable' and reconnect first.",
                     "Save to SQLite", wxOK|wxICON_INFORMATION, this);
        return;
    }

    // Export current session to text
    wxDateTime now = wxDateTime::Now();
    wxString host  = tab->host->GetValue(); host.Replace(".", "_");
    wxString defName = now.Format("%Y%m%d_%H%M%S_") + host + "_export.txt";

    wxFileDialog dlg(this,"Export SQLite to text","",defName,
                     "Text files (*.txt)|*.txt|All files (*)|*",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;

    // sessionId empty = export all, non-empty = export session
    bool ok = tab->db->ExportText(tab->sessionId, dlg.GetPath().ToStdString());
    if (ok)
        SetStatusText("Exported: " + dlg.GetPath());
    else
        SetStatusText("Export failed - check DB path");
}

void MainFrame::OnMenuOpenDb(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Open SQLite Database","","",
                     "SQLite files (*.db)|*.db|All files (*)|*",
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal()!=wxID_OK) return;
    OpenDatabaseInTab(dlg.GetPath());
}

void MainFrame::OnMenuConnect(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    SetStatusText("Connecting..."); SaveSettings();
    DoConnect(*tab, CurrentTabIndex());
}
void MainFrame::OnMenuDisconnect(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(tab) DoDisconnect(*tab);
    SetStatusText("Disconnected");
}

void MainFrame::OnMenuManageProfiles(wxCommandEvent&)
{
    wxDialog* dlg = CreateProfileDialog(this, m_profiles);
    dlg->ShowModal(); dlg->Destroy();
    RebuildProfileMenu();
}

void MainFrame::OnMenuLoadProfile(wxCommandEvent& evt)
{
    int idx = evt.GetId()-ID_MENU_LOAD_PROFILE_BASE;
    const auto& profs = m_profiles.Profiles();
    if (idx<0 || (std::size_t)idx>=profs.size()) return;
    const auto& p = profs[(std::size_t)idx];
    auto* tab=CurrentTab(); if(!tab) return;
    tab->host->SetValue(wxString::FromUTF8(p.host));
    tab->port->SetValue(p.port);
    tab->user->SetValue(wxString::FromUTF8(p.user));
    tab->password->SetValue(wxString::FromUTF8(p.password));
    tab->filter->SetValue(wxString::FromUTF8(p.filter));
    tab->journalArgs->SetValue(wxString::FromUTF8(p.journalArgs));
    tab->profileIndex=idx;
    UpdateTabTitle(CurrentTabIndex());
    SetStatusText("Profile: " + wxString::FromUTF8(p.name.empty()?p.host:p.name));
}

void MainFrame::OnMenuApplyFilter(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    std::string f = tab->filter->GetValue().ToStdString();
    if (f.empty()) { tab->logTable->ClearFilter(); tab->activeFilter.clear();
                     SetStatusText("Filter cleared"); return; }
    std::size_t shown = tab->logTable->ApplyFilter(f);
    tab->activeFilter = f;
    SetStatusText(wxString::Format("Filter: %zu rows shown", shown));
}

void MainFrame::OnMenuClearFilter(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    tab->logTable->ClearFilter(); tab->activeFilter.clear();
    SetStatusText("Filter cleared");
}

void MainFrame::OnMenuUploadFile(wxCommandEvent&)
{
    auto* tab=CurrentTab();
    if (!tab || !tab->reader->IsRunning()) { SetStatusText("Not connected"); return; }
    wxFileDialog fDlg(this,"Select file","","","*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (fDlg.ShowModal()!=wxID_OK) return;
    wxTextEntryDialog dDlg(this,"Remote directory:","SCP Upload","/tmp");
    if (dDlg.ShowModal()!=wxID_OK) return;
    SetStatusText("Uploading...");
    if (tab->reader->UploadFile(fDlg.GetPath().ToStdString(), dDlg.GetValue().ToStdString()))
        SetStatusText("Upload complete");
    else
        SetStatusText("Upload failed");
}

void MainFrame::OnMenuExecuteCommand(wxCommandEvent&)
{
    auto* tab=CurrentTab();
    if (!tab || !tab->reader->IsRunning()) { SetStatusText("Not connected"); return; }
    wxTextEntryDialog dlg(this,"Remote command:","Run Command","uname -a");
    if (dlg.ShowModal()!=wxID_OK) return;
    std::string output;
    SetStatusText("Running...");
    if (!tab->reader->ExecuteCommand(dlg.GetValue().ToStdString(), output))
    { SetStatusText("Command failed"); return; }
    auto& out = AddConnectionTab("$ "+dlg.GetValue().Left(20), true);
    out.host->SetValue(tab->host->GetValue());
    out.logTable->AppendLine("$ "+dlg.GetValue().ToStdString()+"\n\n"+output);
    ApplyTheme(); SaveSettings();
    SetStatusText("Command output in new tab");
}

void MainFrame::OnMenuDarkTheme(wxCommandEvent& evt)
{
    m_darkEnabled=evt.IsChecked();
    ApplyTheme(); SaveSettings();
}

void MainFrame::OnMenuFilterConfig(wxCommandEvent&)
{
    wxDialog* dlg = CreateFilterConfigDialog(this, m_filterConfig);
    dlg->ShowModal(); dlg->Destroy();
    ApplyFilterConfigToAllTabs();
    // Auto-save on close
    m_filterConfig.SaveToFile("filters.json");
}

void MainFrame::OnMenuLoadFilterJson(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Load Filter JSON","","",
                     "JSON files (*.json)|*.json|All files (*)|*",
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal()!=wxID_OK) return;
    if (m_filterConfig.LoadFromFile(dlg.GetPath().ToStdString()))
    {
        ApplyFilterConfigToAllTabs();
        SetStatusText("Filters loaded: " + dlg.GetPath());
    }
    else
        wxMessageBox("Failed to load filter JSON","Error",wxOK|wxICON_ERROR,this);
}

void MainFrame::OnMenuSaveFilterJson(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Save Filter JSON","","filters.json",
                     "JSON files (*.json)|*.json|All files (*)|*",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;
    if (m_filterConfig.SaveToFile(dlg.GetPath().ToStdString()))
        SetStatusText("Filters saved: " + dlg.GetPath());
    else
        wxMessageBox("Failed to save filter JSON","Error",wxOK|wxICON_ERROR,this);
}

// ─────────────────────────────────────────────
void MainFrame::OnSSHLog(wxThreadEvent& evt)
{
    AppendLog((std::size_t)evt.GetInt(), evt.GetString().ToStdString());
}

void MainFrame::OnSSHStatus(wxThreadEvent& evt)
{
    std::size_t idx = (std::size_t)evt.GetInt();
    wxString    status = evt.GetString();
    SetStatusText(wxString::Format("Tab %zu: %s", idx+1, status));
    if (idx < m_tabs.size())
    {
        wxString suf = (status=="connected") ? wxString(" [on]") : wxString(" [off]");
        m_notebook->SetPageText((int)idx, m_tabs[idx]->host->GetValue()+suf);
    }
}

void MainFrame::OnClose(wxCloseEvent& evt)
{
    SaveSettings();
    m_filterConfig.SaveToFile("filters.json");
    evt.Skip();
}

bool JournalApp::OnInit()
{
    wxApp::SetVendorName("DRSSH");
    wxApp::SetAppName("RemoteJournal");
    auto* frame = new MainFrame();
    frame->Show();
    return true;
}
