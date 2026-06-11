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
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/clrpicker.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>

#include "MainFrame.h"
#include "Events.h"
#include "LogEntry.h"
#include "Database.h"

#include <fstream>
#include <regex>

wxDialog* CreateProfileDialog(wxWindow* parent, ProfileManager& mgr);
wxDialog* CreateFilterConfigDialog(wxWindow* parent, FilterConfig& cfg);

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
    EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, MainFrame::OnNotebookPageChanged)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(JournalApp);

// ─────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Remote Journalctl Viewer",
              wxDefaultPosition, wxSize(1500, 900))
{
    SetMinSize(wxSize(1000, 600));
    BuildMenu();
    CreateControls();
    Bind(EVT_SSH_LOG,    &MainFrame::OnSSHLog,    this);
    Bind(EVT_SSH_STATUS, &MainFrame::OnSSHStatus,  this);
    CreateStatusBar(2);
    SetStatusText("Ready");

    // Keyboard accelerators for font zoom
    wxAcceleratorEntry accel[4];
    accel[0].Set(wxACCEL_CTRL, (int)'=', ID_MENU_FONT_INC);
    accel[1].Set(wxACCEL_CTRL, (int)'+', ID_MENU_FONT_INC);
    accel[2].Set(wxACCEL_CTRL, (int)'-', ID_MENU_FONT_DEC);
    accel[3].Set(wxACCEL_CTRL, WXK_NUMPAD_ADD, ID_MENU_FONT_INC);
    SetAcceleratorTable(wxAcceleratorTable(4, accel));

    LoadSettings();
    LoadAutoStart();
}

MainFrame::~MainFrame()
{
    for (auto& tab : m_tabs)
        if (tab->reader) { tab->reader->SetCallback(nullptr); tab->reader->Stop(); }
}

// ─────────────────────────────────────────────
void MainFrame::BuildMenu()
{
    m_menuBar = new wxMenuBar;

    auto* file = new wxMenu;
    file->Append(ID_MENU_NEW_TAB,   "&New Tab\tCtrl+T");
    file->Append(ID_MENU_CLOSE_TAB, "&Close Tab\tCtrl+W");
    file->AppendSeparator();
    file->Append(ID_MENU_SAVE,      "&Save Logs (text)...\tCtrl+S");
    file->Append(ID_MENU_SAVE_DB,   "Save / Export &SQLite...");
    file->Append(ID_MENU_OPEN_DB,   "&Open SQLite DB...\tCtrl+O");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    m_menuBar->Append(file, "&File");

    auto* conn = new wxMenu;
    conn->Append(ID_MENU_CONNECT,         "&Connect\tCtrl+K");
    conn->Append(ID_MENU_DISCONNECT,      "&Disconnect\tCtrl+D");
    conn->AppendSeparator();
    conn->Append(ID_MENU_MANAGE_PROFILES, "&Manage Profiles...");
    conn->Append(ID_MENU_AUTOSTART,       "&AutoStart Settings...");
    conn->AppendSeparator();
    m_profileMenu = new wxMenu;
    conn->AppendSubMenu(m_profileMenu, "Load &Profile");
    m_menuBar->Append(conn, "&Connection");

    auto* tools = new wxMenu;
    tools->Append(ID_MENU_APPLY_FILTER,    "Apply &Filter\tCtrl+F");
    tools->Append(ID_MENU_CLEAR_FILTER,    "Clear Filter");
    tools->AppendSeparator();
    tools->Append(ID_MENU_UPLOAD_FILE,     "&Upload File...");
    tools->Append(ID_MENU_EXECUTE_COMMAND, "Run &Command...\tCtrl+R");
    m_menuBar->Append(tools, "&Tools");

    auto* filters = new wxMenu;
    filters->Append(ID_MENU_FILTER_CONFIG,    "&Configure Filters...\tCtrl+Shift+F");
    filters->AppendSeparator();
    filters->Append(ID_MENU_LOAD_FILTER_JSON, "&Load Filter JSON...");
    filters->Append(ID_MENU_SAVE_FILTER_JSON, "&Save Filter JSON...");
    m_menuBar->Append(filters, "F&ilters");

    auto* view = new wxMenu;
    view->AppendCheckItem(ID_MENU_DARK_THEME, "&Dark Theme\tCtrl+Shift+D");
    view->AppendSeparator();
    view->Append(ID_MENU_FONT_INC, "Font &Larger\tCtrl+=");
    view->Append(ID_MENU_FONT_DEC, "Font &Smaller\tCtrl+-");
    m_menuBar->Append(view, "&View");

    SetMenuBar(m_menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnMenuNewTab,           this, ID_MENU_NEW_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuCloseTab,         this, ID_MENU_CLOSE_TAB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSave,             this, ID_MENU_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSaveDb,           this, ID_MENU_SAVE_DB);
    Bind(wxEVT_MENU, &MainFrame::OnMenuOpenDb,           this, ID_MENU_OPEN_DB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ Close(); },       wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuConnect,          this, ID_MENU_CONNECT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuDisconnect,       this, ID_MENU_DISCONNECT);
    Bind(wxEVT_MENU, &MainFrame::OnMenuManageProfiles,   this, ID_MENU_MANAGE_PROFILES);
    Bind(wxEVT_MENU, &MainFrame::OnMenuAutoStartSettings,this, ID_MENU_AUTOSTART);
    Bind(wxEVT_MENU, &MainFrame::OnMenuApplyFilter,      this, ID_MENU_APPLY_FILTER);
    Bind(wxEVT_MENU, &MainFrame::OnMenuClearFilter,      this, ID_MENU_CLEAR_FILTER);
    Bind(wxEVT_MENU, &MainFrame::OnMenuUploadFile,       this, ID_MENU_UPLOAD_FILE);
    Bind(wxEVT_MENU, &MainFrame::OnMenuExecuteCommand,   this, ID_MENU_EXECUTE_COMMAND);
    Bind(wxEVT_MENU, &MainFrame::OnMenuDarkTheme,        this, ID_MENU_DARK_THEME);
    Bind(wxEVT_MENU, &MainFrame::OnMenuFontIncrease,     this, ID_MENU_FONT_INC);
    Bind(wxEVT_MENU, &MainFrame::OnMenuFontDecrease,     this, ID_MENU_FONT_DEC);
    Bind(wxEVT_MENU, &MainFrame::OnMenuFilterConfig,     this, ID_MENU_FILTER_CONFIG);
    Bind(wxEVT_MENU, &MainFrame::OnMenuLoadFilterJson,   this, ID_MENU_LOAD_FILTER_JSON);
    Bind(wxEVT_MENU, &MainFrame::OnMenuSaveFilterJson,   this, ID_MENU_SAVE_FILTER_JSON);
}

void MainFrame::CreateControls()
{
    m_notebook = new wxNotebook(this, wxID_ANY);
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_notebook, 1, wxEXPAND | wxALL, 4);
    SetSizer(root);
}

// ─────────────────────────────────────────────
// Sidebar builder — left panel per tab
// ─────────────────────────────────────────────
wxPanel* MainFrame::BuildSidebar(wxWindow* parent, ConnectionTab& tab)
{
    // Scrollable sidebar so nothing gets cut off on small screens
    auto* scroll = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition,
                                        wxSize(280, 400), wxVSCROLL);
    scroll->SetScrollRate(0, 8);
    scroll->SetMinSize(wxSize(220, 200));  // prevent GTK scrollbar at size <= 0

    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── Connection box ───────────────────────────────────
    auto* connBox = new wxStaticBoxSizer(wxVERTICAL, scroll, "Connection");
    auto* grid = new wxFlexGridSizer(2, 4, 6);   // 2 cols
    grid->AddGrowableCol(1);

    auto addLbl = [&](const wxString& t){
        grid->Add(new wxStaticText(scroll,wxID_ANY,t), 0, wxALIGN_CENTER_VERTICAL);
    };
    auto addFld = [&](const wxString& lbl, wxTextCtrl*& ctrl, long style=0){
        addLbl(lbl);
        ctrl = new wxTextCtrl(scroll,wxID_ANY,"",wxDefaultPosition,wxDefaultSize,style);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    addFld("Host:",     tab.host);      tab.host->SetHint("192.168.1.1");
    addLbl("Port:");
    tab.port = new wxSpinCtrl(scroll,wxID_ANY,"22",wxDefaultPosition,
                               wxDefaultSize,wxSP_ARROW_KEYS,1,65535,22);
    grid->Add(tab.port, 1, wxEXPAND);
    addFld("User:",     tab.user);      tab.user->SetHint("root");
    addFld("Password:", tab.password,   wxTE_PASSWORD);

    connBox->Add(grid, 0, wxEXPAND | wxALL, 6);

    // Connect / Disconnect buttons
    auto* connBtns = new wxBoxSizer(wxHORIZONTAL);
    auto* btnConn  = new wxButton(scroll,wxID_ANY,"Connect");
    auto* btnDisco = new wxButton(scroll,wxID_ANY,"Disconnect");
    connBtns->Add(btnConn,  1, wxEXPAND | wxRIGHT, 4);
    connBtns->Add(btnDisco, 1, wxEXPAND);
    connBox->Add(connBtns, 0, wxEXPAND | wxALL, 6);

    addFld("jctl args:", tab.journalArgs); tab.journalArgs->SetHint("-f -o short-iso");
    connBox->Add(grid, 0, wxEXPAND);  // journalArgs already added to grid

    // SQLite
    auto* sqlRow = new wxBoxSizer(wxHORIZONTAL);
    tab.useSqlite = new wxCheckBox(scroll,wxID_ANY,"SQLite");
    tab.dbPath    = new wxTextCtrl(scroll,wxID_ANY);
    tab.dbPath->SetHint("auto: YYYYMMDD_host.db");
    sqlRow->Add(tab.useSqlite, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT,4);
    sqlRow->Add(tab.dbPath,    1, wxEXPAND);
    connBox->Add(sqlRow, 0, wxEXPAND|wxALL, 6);

    root->Add(connBox, 0, wxEXPAND|wxALL, 4);

    // ── Filter box ──────────────────────────────────────
    auto* filtBox = new wxStaticBoxSizer(wxVERTICAL, scroll, "Filters");

    // Positive filter
    filtBox->Add(new wxStaticText(scroll,wxID_ANY,"Show (regex):"), 0, wxBOTTOM, 2);
    tab.filter = new wxTextCtrl(scroll,wxID_ANY);
    tab.filter->SetHint("error|warn|sshd");
    filtBox->Add(tab.filter, 0, wxEXPAND|wxBOTTOM, 6);

    // Negative filter
    filtBox->Add(new wxStaticText(scroll,wxID_ANY,"Hide (regex):"), 0, wxBOTTOM, 2);
    tab.negFilter = new wxTextCtrl(scroll,wxID_ANY);
    tab.negFilter->SetHint("DEBUG|audit|cron");
    filtBox->Add(tab.negFilter, 0, wxEXPAND|wxBOTTOM, 6);

    // Filter action buttons
    auto* fltBtns = new wxBoxSizer(wxHORIZONTAL);
    auto* btnApply    = new wxButton(scroll,wxID_ANY,"Apply",    wxDefaultPosition,wxSize(-1,28));
    auto* btnClear    = new wxButton(scroll,wxID_ANY,"Clear",    wxDefaultPosition,wxSize(-1,28));
    auto* btnSaveFilt = new wxButton(scroll,wxID_ANY,"Save Lines",wxDefaultPosition,wxSize(-1,28));
    fltBtns->Add(btnApply,    1, wxEXPAND|wxRIGHT,3);
    fltBtns->Add(btnClear,    1, wxEXPAND|wxRIGHT,3);
    fltBtns->Add(btnSaveFilt, 1, wxEXPAND);
    filtBox->Add(fltBtns, 0, wxEXPAND);

    root->Add(filtBox, 0, wxEXPAND|wxALL, 4);

    // ── Font box ────────────────────────────────────────
    auto* fontBox = new wxStaticBoxSizer(wxVERTICAL, scroll, "Log Font");
    auto* fontGrid = new wxFlexGridSizer(2, 4, 6);
    fontGrid->AddGrowableCol(1);

    fontGrid->Add(new wxStaticText(scroll,wxID_ANY,"Size:"), 0, wxALIGN_CENTER_VERTICAL);
    tab.fontSize = new wxSpinCtrl(scroll,wxID_ANY,"9",wxDefaultPosition,
                                   wxDefaultSize,wxSP_ARROW_KEYS,6,32,9);
    fontGrid->Add(tab.fontSize, 1, wxEXPAND);

    fontGrid->Add(new wxStaticText(scroll,wxID_ANY,"Face:"), 0, wxALIGN_CENTER_VERTICAL);
    tab.fontFace = new wxTextCtrl(scroll,wxID_ANY,"");
    tab.fontFace->SetHint("monospace");
    fontGrid->Add(tab.fontFace, 1, wxEXPAND);

    fontBox->Add(fontGrid, 0, wxEXPAND|wxALL, 6);

    auto* btnApplyFont = new wxButton(scroll,wxID_ANY,"Apply Font",
                                       wxDefaultPosition,wxSize(-1,28));
    fontBox->Add(btnApplyFont, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 6);

    root->Add(fontBox, 0, wxEXPAND|wxALL, 4);

    // Push everything to the top — spacer eats remaining vertical space
    root->AddStretchSpacer(1);

    scroll->SetSizer(root);
    // Use FitInside so the virtual size matches content, keeping it top-aligned
    root->FitInside(scroll);

    // ── Button bindings ─────────────────────────────────
    btnConn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        auto* t=CurrentTab(); if(!t) return;
        SetStatusText("Connecting..."); SaveSettings();
        DoConnect(*t,CurrentTabIndex());
    });
    btnDisco->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        auto* t=CurrentTab(); if(t) DoDisconnect(*t);
        SetStatusText("Disconnected");
    });
    btnApply->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e){ OnMenuApplyFilter(e); });
    btnClear->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e){ OnMenuClearFilter(e); });

    btnSaveFilt->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        auto* t = CurrentTab();
        if (!t) return;

        const auto& vis = t->logTable->VisibleEntries();
        if (vis.empty()) { SetStatusText("No filtered lines to save"); return; }

        wxDateTime now=wxDateTime::Now();
        wxString host=t->host->GetValue(); host.Replace(".","-");
        wxString def=now.Format("%Y%m%d_%H%M%S_")+host+"_filtered.txt";
        wxFileDialog dlg(this,"Save Filtered Lines","",def,
                         "Text files (*.txt)|*.txt|All (*)|*",
                         wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal()!=wxID_OK) return;
        std::ofstream out(dlg.GetPath().ToStdString());
        for (const auto& e : vis) out << e.raw << '\n';
        SetStatusText(wxString::Format("Saved %zu lines", vis.size()));
    });

    btnApplyFont->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        ApplyLogFontFromCurrentTab();
    });

    // Ctrl+= / Ctrl++ / Ctrl+- handled at frame level (see BuildMenu/ctor)
    return scroll;
}

// ─────────────────────────────────────────────
ConnectionTab& MainFrame::AddConnectionTab(const wxString& title, bool select)
{
    auto tab = std::make_unique<ConnectionTab>();
    tab->page = new wxPanel(m_notebook, wxID_ANY);

    // Outer horizontal splitter: left sidebar | right log area
    auto* hSplit = new wxSplitterWindow(tab->page, wxID_ANY,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_LIVE_UPDATE|wxSP_THIN_SASH);

    // Build sidebar (left)
    wxPanel* sidebar = BuildSidebar(hSplit, *tab);

    // Right area: vertical splitter: log table | payload
    auto* vSplit = new wxSplitterWindow(hSplit, wxID_ANY,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_LIVE_UPDATE|wxSP_THIN_SASH);

    tab->logTable    = new LogTableCtrl(vSplit);
    tab->payloadCtrl = new wxTextCtrl(vSplit, wxID_ANY, "",
                                       wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE|wxTE_READONLY|wxTE_RICH2);
    tab->payloadCtrl->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    tab->logTable->SetPayloadCtrl(tab->payloadCtrl);
    tab->logTable->SetFilterConfig(&m_filterConfig);

    vSplit->SplitHorizontally(tab->logTable, tab->payloadCtrl, -180);
    vSplit->SetMinimumPaneSize(80);  // prevents GTK scrollbar negative-size crash
    vSplit->SetSashGravity(1.0);     // log table takes all resize space

    hSplit->SplitVertically(sidebar, vSplit, 280);
    hSplit->SetMinimumPaneSize(220);
    hSplit->SetSashGravity(0.0);     // sidebar keeps its width on frame resize

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(hSplit, 1, wxEXPAND);
    tab->page->SetSizer(root);

    tab->reader = std::make_unique<SSHJournalReader>();

    auto& ref = *tab;
    m_tabs.push_back(std::move(tab));
    m_notebook->AddPage(ref.page, title, select);
    return ref;
}

void MainFrame::UpdateTabTitle(std::size_t index)
{
    auto* t=TabAt(index); if(!t) return;
    wxString label=t->host->GetValue();
    if (label.IsEmpty()) label=wxString::Format("Tab %zu",index+1);
    m_notebook->SetPageText((int)index,label);
}

void MainFrame::CloseConnectionTab(std::size_t index)
{
    if (m_tabs.size()==1){SetStatusText("Cannot close the last tab");return;}
    auto* t=TabAt(index); if(!t) return;

    // 1. Stop background thread FIRST so no EVT_SSH_LOG arrives for this index.
    if (t->reader){t->reader->SetCallback(nullptr);t->reader->Stop();}
    if (t->db) t->db->Close();

    // 2. Remove from our vector BEFORE telling the notebook to destroy the page.
    //    This ensures AppendLog() cannot touch a dead index even if a queued
    //    event slips through between step 1 and the DeletePage call.
    m_tabs.erase(m_tabs.begin()+(std::ptrdiff_t)index);

    // 3. Now it is safe to destroy the wx widgets.
    m_notebook->DeletePage((int)index);

    // 4. Clamp selection.
    if (!m_tabs.empty())
    {
        std::size_t next=(index>=m_tabs.size())?m_tabs.size()-1:index;
        m_notebook->SetSelection((int)next);
    }
    SaveSettings();
}

ConnectionTab* MainFrame::CurrentTab() { return TabAt(CurrentTabIndex()); }
ConnectionTab* MainFrame::TabAt(std::size_t i)
{
    return (i<m_tabs.size())?m_tabs[i].get():nullptr;
}
std::size_t MainFrame::CurrentTabIndex() const
{
    if (!m_notebook||m_notebook->GetPageCount()==0) return 0;
    int s=m_notebook->GetSelection();
    return (s==wxNOT_FOUND)?0:(std::size_t)s;
}

// ─────────────────────────────────────────────
void MainFrame::AppendLog(std::size_t tabIndex, const std::string& line)
{
    auto* tab=TabAt(tabIndex); if(!tab) return;
    if (tab->db&&tab->db->IsOpen()&&!tab->sessionId.empty())
        tab->db->InsertLog(tab->sessionId,LogEntry::Parse(line));
    tab->logTable->AppendLine(line);
}

void MainFrame::ApplyFilterConfigToAllTabs()
{
    for (auto& t:m_tabs)
        if (t->logTable){t->logTable->SetFilterConfig(&m_filterConfig);t->logTable->Refresh();}
}

// ─────────────────────────────────────────────
void MainFrame::RebuildProfileMenu()
{
    while (m_profileMenu->GetMenuItemCount()>0)
        m_profileMenu->Destroy(m_profileMenu->FindItemByPosition(0));
    const auto& profs=m_profiles.Profiles();
    if (profs.empty()){m_profileMenu->Append(ID_MENU_LOAD_PROFILE_BASE,"(no profiles)");return;}
    for (std::size_t i=0;i<profs.size()&&i<50;++i)
    {
        int id=ID_MENU_LOAD_PROFILE_BASE+(int)i;
        m_profileMenu->Append(id,wxString::FromUTF8(profs[i].name.empty()?profs[i].host:profs[i].name));
        Bind(wxEVT_MENU,&MainFrame::OnMenuLoadProfile,this,id);
    }
}

// ─────────────────────────────────────────────
void MainFrame::LoadAutoStart()
{
    if (!wxFileExists("autostart.json")) return;

    // Ask for encryption key if file exists
    wxPasswordEntryDialog dlg(this,
        "autostart.json found.\nEnter encryption key to unlock sessions\n(leave blank for default key):",
        "Unlock AutoStart");
    std::string key = m_autoStartKey;
    if (dlg.ShowModal()==wxID_OK && !dlg.GetValue().IsEmpty())
        key = dlg.GetValue().ToStdString();

    if (!m_autoStart.Load("autostart.json", key))
    {
        SetStatusText("Could not load autostart.json");
        return;
    }

    // Apply sessions: each becomes a tab; auto-connect if flagged
    for (std::size_t i=0; i<m_autoStart.Sessions().size(); ++i)
    {
        const auto& s = m_autoStart.Sessions()[i];
        wxString title = wxString::FromUTF8(
            s.profileName.empty() ? s.host : s.profileName);

        // Add tab if we need more than we already have
        while (m_tabs.size() <= i)
            AddConnectionTab(wxString::Format("Connection %zu", m_tabs.size()+1), false);

        auto* tab = TabAt(i);
        if (!tab) continue;

        tab->host->SetValue(wxString::FromUTF8(s.host));
        tab->port->SetValue(s.port);
        tab->user->SetValue(wxString::FromUTF8(s.user));
        tab->password->SetValue(wxString::FromUTF8(s.encryptedPass)); // already decrypted
        tab->journalArgs->SetValue(wxString::FromUTF8(s.journalArgs));
        tab->filter->SetValue(wxString::FromUTF8(s.filter));
        tab->negFilter->SetValue(wxString::FromUTF8(s.negFilter));
        tab->useSqlite->SetValue(s.useSqlite);
        tab->dbPath->SetValue(wxString::FromUTF8(s.dbPath));
        UpdateTabTitle(i);

        if (s.autoConnect)
            DoConnect(*tab, i);
    }
    if (!m_tabs.empty()) m_notebook->SetSelection(0);
    SetStatusText("AutoStart loaded");
}

void MainFrame::SaveAutoStart()
{
    m_autoStart.Sessions().clear();
    for (const auto& t : m_tabs)
    {
        AutoStartSession s;
        s.profileName   = m_notebook->GetPageText(
                              (int)(&t - &m_tabs[0])).ToStdString();
        s.host          = t->host->GetValue().ToStdString();
        s.port          = t->port->GetValue();
        s.user          = t->user->GetValue().ToStdString();
        s.encryptedPass = t->password->GetValue().ToStdString(); // Save will encrypt
        s.journalArgs   = t->journalArgs->GetValue().ToStdString();
        s.filter        = t->filter->GetValue().ToStdString();
        s.negFilter     = t->negFilter->GetValue().ToStdString();
        s.useSqlite     = t->useSqlite->GetValue();
        s.dbPath        = t->dbPath->GetValue().ToStdString();
        s.autoConnect   = t->reader && t->reader->IsRunning();
        m_autoStart.Sessions().push_back(std::move(s));
    }
    m_autoStart.Save("autostart.json", m_autoStartKey);
}

// ─────────────────────────────────────────────
void MainFrame::LoadSettings()
{
    m_profiles.Load();
    RebuildProfileMenu();
    m_filterConfig.LoadFromFile("filters.json");

    wxConfig cfg("RemoteJournal");
    long count=0; cfg.Read("tabs/count",&count,0);

    if (count<=0)
    {
        AddConnectionTab("Connection 1",true);
    }
    else
    {
        for (long i=0;i<count;++i)
        {
            auto& tab=AddConnectionTab(wxString::Format("Connection %ld",i+1),i==0);
            wxString base=wxString::Format("tabs/%ld/",i);
            wxString tmp; long port=22; bool useSql=false;
            cfg.Read(base+"host",&tmp,"");        tab.host->SetValue(tmp);
            cfg.Read(base+"port",&port,22);       tab.port->SetValue((int)port);
            cfg.Read(base+"user",&tmp,"");        tab.user->SetValue(tmp);
            cfg.Read(base+"filter",&tmp,"");      tab.filter->SetValue(tmp);
            cfg.Read(base+"negFilter",&tmp,"");   tab.negFilter->SetValue(tmp);
            cfg.Read(base+"journalArgs",&tmp,""); tab.journalArgs->SetValue(tmp);
            cfg.Read(base+"useSqlite",&useSql,false); tab.useSqlite->SetValue(useSql);
            cfg.Read(base+"dbPath",&tmp,"");      tab.dbPath->SetValue(tmp);
            UpdateTabTitle((std::size_t)i);
        }
    }

    long sel=0; cfg.Read("tabs/selected",&sel,0);
    if (sel>=0&&sel<(long)m_tabs.size()) m_notebook->SetSelection((int)sel);

    bool dark=false; cfg.Read("ui/dark",&dark,false);
    m_darkEnabled=dark;
    if (auto* mi=m_menuBar->FindItem(ID_MENU_DARK_THEME)) mi->Check(m_darkEnabled);
    ApplyTheme();

    bool max=false; cfg.Read("window/maximized",&max,false);
    long x=50,y=50,w=1500,h=900;
    if (cfg.Read("window/x",&x)&&cfg.Read("window/y",&y)&&
        cfg.Read("window/w",&w)&&cfg.Read("window/h",&h))
        SetSize((int)x,(int)y,(int)w,(int)h);
    if (max) Maximize();
}

void MainFrame::SaveSettings()
{
    wxConfig cfg("RemoteJournal");
    cfg.DeleteGroup("tabs");
    cfg.Write("tabs/count",(long)m_tabs.size());
    cfg.Write("tabs/selected",(long)CurrentTabIndex());
    for (std::size_t i=0;i<m_tabs.size();++i)
    {
        const auto& t=*m_tabs[i];
        wxString base=wxString::Format("tabs/%zu/",i);
        cfg.Write(base+"host",        t.host->GetValue());
        cfg.Write(base+"port",        (long)t.port->GetValue());
        cfg.Write(base+"user",        t.user->GetValue());
        cfg.Write(base+"filter",      t.filter->GetValue());
        cfg.Write(base+"negFilter",   t.negFilter->GetValue());
        cfg.Write(base+"journalArgs", t.journalArgs->GetValue());
        cfg.Write(base+"useSqlite",   t.useSqlite->GetValue());
        cfg.Write(base+"dbPath",      t.dbPath->GetValue());
    }
    cfg.Write("ui/dark",m_darkEnabled);
    cfg.Write("window/maximized",IsMaximized());
    if (!IsMaximized()){
        wxPoint p=GetPosition();wxSize s=GetSize();
        cfg.Write("window/x",(long)p.x);cfg.Write("window/y",(long)p.y);
        cfg.Write("window/w",(long)s.x);cfg.Write("window/h",(long)s.y);
    }
    cfg.Flush();
}

// ─────────────────────────────────────────────
bool MainFrame::DoConnect(ConnectionTab& tab, std::size_t tabIndex)
{
    if (tab.reader->IsRunning()) DoDisconnect(tab);

    if (tab.useSqlite->GetValue())
    {
        if (tab.db) tab.db->Close();
        wxString path=tab.dbPath->GetValue().Trim();
        if (path.IsEmpty())
        {
            wxDateTime now=wxDateTime::Now();
            wxString host=tab.host->GetValue(); host.Replace(".","_");
            path=now.Format("%Y%m%d_%H%M%S_")+host+".db";
            tab.dbPath->SetValue(path);
        }
        tab.db=std::make_unique<Database>();
        if (!tab.db->Open(path.ToStdString()))
        {
            wxMessageBox("Cannot open SQLite DB:\n"+path,"DB Error",wxOK|wxICON_ERROR,this);
            tab.db.reset();
        }
        else
        {
            tab.sessionId=wxDateTime::Now().Format("%Y%m%d%H%M%S").ToStdString()
                         +"_"+tab.host->GetValue().ToStdString();
        }
    }

    tab.reader->SetCallback([this,tabIndex](const std::string& line){
        auto* evt=new wxThreadEvent(EVT_SSH_LOG);
        evt->SetInt((int)tabIndex);
        evt->SetString(wxString::FromUTF8(line));
        wxQueueEvent(this,evt);
    });

    std::string jargs=tab.journalArgs->GetValue().ToStdString();
    if (jargs.empty()) jargs="-f -o short-iso";

    bool ok=tab.reader->Connect(
        tab.host->GetValue().ToStdString(), tab.port->GetValue(),
        tab.user->GetValue().ToStdString(), tab.password->GetValue().ToStdString());
    if (!ok){
        tab.reader->SetCallback(nullptr);
        wxMessageBox("SSH connection failed.","Connect",wxOK|wxICON_ERROR,this);
        return false;
    }
    if (!tab.reader->Start("journalctl "+jargs)){
        tab.reader->SetCallback(nullptr); tab.reader->Stop();
        wxMessageBox("journalctl failed to start.","Connect",wxOK|wxICON_ERROR,this);
        return false;
    }
    UpdateTabTitle(tabIndex);
    auto* se=new wxThreadEvent(EVT_SSH_STATUS);
    se->SetInt((int)tabIndex); se->SetString("connected");
    wxQueueEvent(this,se);
    return true;
}

void MainFrame::DoDisconnect(ConnectionTab& tab)
{
    tab.reader->SetCallback(nullptr); tab.reader->Stop();
    if (tab.db) tab.db->Close();
}

void MainFrame::SaveLogsToFile(ConnectionTab& tab)
{
    const auto& entries=tab.logTable->AllEntries();
    if (entries.empty()){SetStatusText("No logs");return;}
    wxDateTime now=wxDateTime::Now();
    wxString host=tab.host->GetValue(); host.Replace(".","_");
    wxString def=now.Format("%Y%m%d_%H%M%S_")+host+".txt";
    wxFileDialog dlg(this,"Save Logs","",def,
                     "Text files (*.txt)|*.txt|All (*)|*",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;
    std::ofstream out(dlg.GetPath().ToStdString());
    for (const auto& e:entries) out<<e.raw<<'\n';
    SetStatusText("Saved: "+dlg.GetPath());
}

void MainFrame::OpenDatabaseInTab(const wxString& path)
{
    auto db=std::make_unique<Database>();
    if (!db->Open(path.ToStdString())){
        wxMessageBox("Cannot open:\n"+path,"Open DB",wxOK|wxICON_ERROR,this);
        return;
    }
    auto& tab=AddConnectionTab(wxFileName(path).GetFullName(),true);
    tab.host->SetValue(path); tab.host->SetEditable(false);
    tab.port->Disable(); tab.user->Disable(); tab.password->Disable();
    tab.useSqlite->Disable(); tab.dbPath->Disable();
    auto entries=db->QueryAllLogs();
    tab.logTable->SetEntries(entries);
    tab.logTable->SetFilterConfig(&m_filterConfig);
    tab.db=std::move(db);
    tab.sessionId="";
    ApplyTheme();
    SetStatusText(wxString::Format("Loaded %zu rows from %s",entries.size(),path));
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
    static const wxColour bg(30,31,34),fg(220,220,220),field(20,21,24);
    if (dynamic_cast<wxTextCtrl*>(w)||dynamic_cast<wxListCtrl*>(w))
    {w->SetBackgroundColour(field);w->SetForegroundColour(fg);}
    else{w->SetBackgroundColour(bg);w->SetForegroundColour(fg);}
    for(auto* c:w->GetChildren()) ApplyDarkTheme(c);
}
void MainFrame::ApplyLightTheme(wxWindow* w)
{
    w->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    w->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    for(auto* c:w->GetChildren()) ApplyLightTheme(c);
}

// ─────────────────────────────────────────────
// Menu handlers
// ─────────────────────────────────────────────
void MainFrame::OnMenuNewTab(wxCommandEvent&)
{
    AddConnectionTab(wxString::Format("Connection %zu",m_tabs.size()+1),true);
    ApplyTheme(); SaveSettings();
}
void MainFrame::OnMenuCloseTab(wxCommandEvent&){CloseConnectionTab(CurrentTabIndex());}
void MainFrame::OnMenuSave(wxCommandEvent&){auto* t=CurrentTab();if(t)SaveLogsToFile(*t);}

void MainFrame::OnMenuSaveDb(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    if (!tab->db||!tab->db->IsOpen()){
        wxMessageBox("SQLite not enabled for this tab.\nEnable it and reconnect.",
                     "SQLite",wxOK|wxICON_INFORMATION,this);
        return;
    }
    wxDateTime now=wxDateTime::Now();
    wxString host=tab->host->GetValue(); host.Replace(".","_");
    wxString def=now.Format("%Y%m%d_%H%M%S_")+host+"_export.txt";
    wxFileDialog dlg(this,"Export SQLite","",def,
                     "Text files (*.txt)|*.txt|All (*)|*",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;
    if (tab->db->ExportText(tab->sessionId,dlg.GetPath().ToStdString()))
        SetStatusText("Exported: "+dlg.GetPath());
    else SetStatusText("Export failed");
}

void MainFrame::OnMenuOpenDb(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Open SQLite DB","","",
                     "SQLite files (*.db)|*.db|All (*)|*",
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal()!=wxID_OK) return;
    OpenDatabaseInTab(dlg.GetPath());
}

void MainFrame::OnMenuConnect(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    SetStatusText("Connecting..."); SaveSettings();
    DoConnect(*tab,CurrentTabIndex());
}
void MainFrame::OnMenuDisconnect(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(tab) DoDisconnect(*tab);
    SetStatusText("Disconnected");
}

void MainFrame::OnMenuManageProfiles(wxCommandEvent&)
{
    wxDialog* dlg=CreateProfileDialog(this,m_profiles);
    dlg->ShowModal(); dlg->Destroy();
    RebuildProfileMenu();
}

void MainFrame::OnMenuAutoStartSettings(wxCommandEvent&)
{
    // Simple dialog: let user set the encryption key + save now
    wxPasswordEntryDialog keyDlg(this,
        "Set encryption key for autostart.json\n(empty = keep current):",
        "AutoStart Key");
    if (keyDlg.ShowModal()==wxID_OK && !keyDlg.GetValue().IsEmpty())
        m_autoStartKey=keyDlg.GetValue().ToStdString();

    SaveAutoStart();
    SetStatusText("autostart.json saved");
}

void MainFrame::OnMenuLoadProfile(wxCommandEvent& evt)
{
    int idx=evt.GetId()-ID_MENU_LOAD_PROFILE_BASE;
    const auto& profs=m_profiles.Profiles();
    if (idx<0||(std::size_t)idx>=profs.size()) return;
    const auto& p=profs[(std::size_t)idx];
    auto* tab=CurrentTab(); if(!tab) return;
    tab->host->SetValue(wxString::FromUTF8(p.host));
    tab->port->SetValue(p.port);
    tab->user->SetValue(wxString::FromUTF8(p.user));
    tab->password->SetValue(wxString::FromUTF8(p.password));
    tab->filter->SetValue(wxString::FromUTF8(p.filter));
    tab->journalArgs->SetValue(wxString::FromUTF8(p.journalArgs));
    tab->profileIndex=idx;
    UpdateTabTitle(CurrentTabIndex());
    SetStatusText("Profile: "+wxString::FromUTF8(p.name.empty()?p.host:p.name));
}

void MainFrame::OnMenuApplyFilter(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    std::string pos=tab->filter->GetValue().ToStdString();
    std::string neg=tab->negFilter->GetValue().ToStdString();
    if (pos.empty()&&neg.empty()){
        tab->logTable->ClearFilter(); tab->activeFilter.clear(); tab->activeNegFilter.clear();
        SetStatusText("Filter cleared"); return;
    }
    std::size_t shown=tab->logTable->ApplyFilter(pos,neg);
    tab->activeFilter=pos; tab->activeNegFilter=neg;
    SetStatusText(wxString::Format("Filter: %zu rows shown",shown));
}

void MainFrame::OnMenuClearFilter(wxCommandEvent&)
{
    auto* tab=CurrentTab(); if(!tab) return;
    tab->logTable->ClearFilter();
    tab->activeFilter.clear(); tab->activeNegFilter.clear();
    SetStatusText("Filter cleared");
}

void MainFrame::OnMenuUploadFile(wxCommandEvent&)
{
    auto* tab=CurrentTab();
    if (!tab||!tab->reader->IsRunning()){SetStatusText("Not connected");return;}
    wxFileDialog fDlg(this,"Select file","","","*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (fDlg.ShowModal()!=wxID_OK) return;
    wxTextEntryDialog dDlg(this,"Remote directory:","SCP Upload","/tmp");
    if (dDlg.ShowModal()!=wxID_OK) return;
    SetStatusText("Uploading...");
    if (tab->reader->UploadFile(fDlg.GetPath().ToStdString(),dDlg.GetValue().ToStdString()))
        SetStatusText("Upload complete");
    else SetStatusText("Upload failed");
}

void MainFrame::OnMenuExecuteCommand(wxCommandEvent&)
{
    auto* tab=CurrentTab();
    if (!tab||!tab->reader->IsRunning()){SetStatusText("Not connected");return;}
    wxTextEntryDialog dlg(this,"Remote command:","Run Command","uname -a");
    if (dlg.ShowModal()!=wxID_OK) return;
    std::string output;
    SetStatusText("Running...");
    if (!tab->reader->ExecuteCommand(dlg.GetValue().ToStdString(),output))
    {SetStatusText("Command failed");return;}
    auto& out=AddConnectionTab("$ "+dlg.GetValue().Left(20),true);
    out.host->SetValue(tab->host->GetValue());
    out.logTable->AppendLine("$ "+dlg.GetValue().ToStdString()+"\n\n"+output);
    ApplyTheme(); SaveSettings();
    SetStatusText("Output in new tab");
}

void MainFrame::OnMenuDarkTheme(wxCommandEvent& evt)
{
    m_darkEnabled=evt.IsChecked(); ApplyTheme(); SaveSettings();
}

void MainFrame::OnMenuFilterConfig(wxCommandEvent&)
{
    wxDialog* dlg=CreateFilterConfigDialog(this,m_filterConfig);
    dlg->ShowModal(); dlg->Destroy();
    ApplyFilterConfigToAllTabs();
    m_filterConfig.SaveToFile("filters.json");
}
void MainFrame::OnMenuLoadFilterJson(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Load Filter JSON","","",
                     "JSON files (*.json)|*.json|All (*)|*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal()!=wxID_OK) return;
    if (m_filterConfig.LoadFromFile(dlg.GetPath().ToStdString()))
    {ApplyFilterConfigToAllTabs();SetStatusText("Filters loaded: "+dlg.GetPath());}
    else wxMessageBox("Failed to load","Error",wxOK|wxICON_ERROR,this);
}
void MainFrame::OnMenuSaveFilterJson(wxCommandEvent&)
{
    wxFileDialog dlg(this,"Save Filter JSON","","filters.json",
                     "JSON files (*.json)|*.json|All (*)|*",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal()!=wxID_OK) return;
    if (m_filterConfig.SaveToFile(dlg.GetPath().ToStdString()))
        SetStatusText("Filters saved: "+dlg.GetPath());
    else wxMessageBox("Failed to save","Error",wxOK|wxICON_ERROR,this);
}

// ─────────────────────────────────────────────
void MainFrame::OnSSHLog(wxThreadEvent& evt)
{
    AppendLog((std::size_t)evt.GetInt(),evt.GetString().ToStdString());
}
void MainFrame::OnSSHStatus(wxThreadEvent& evt)
{
    std::size_t idx=(std::size_t)evt.GetInt();
    wxString status=evt.GetString();
    SetStatusText(wxString::Format("Tab %zu: %s",idx+1,status));
    if (idx<m_tabs.size())
    {
        wxString suf=(status=="connected")?wxString(" [on]"):wxString(" [off]");
        m_notebook->SetPageText((int)idx,m_tabs[idx]->host->GetValue()+suf);
    }
}
void MainFrame::OnNotebookPageChanged(wxNotebookEvent&)
{
    // No-op for now; sidebar is per-tab so nothing to sync
}
void MainFrame::OnClose(wxCloseEvent& evt)
{
    SaveSettings();
    SaveAutoStart();
    m_filterConfig.SaveToFile("filters.json");
    evt.Skip();
}

void MainFrame::ApplyLogFont(ConnectionTab& tab, int size, const wxString& face)
{
    if (size < 6)  size = 6;
    if (size > 48) size = 48;

    wxString resolvedFace = face; resolvedFace.Trim(true); resolvedFace.Trim(false);
    if (resolvedFace.IsEmpty()) resolvedFace = "monospace";

    wxFont f(size, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
             false, resolvedFace);
    if (!f.IsOk())
        f = wxFont(size, wxFONTFAMILY_TELETYPE,
                   wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

    if (tab.logTable)    tab.logTable->SetFont(f);
    if (tab.payloadCtrl) tab.payloadCtrl->SetFont(f);

    if (tab.fontSize && tab.fontSize->GetValue() != size)
        tab.fontSize->SetValue(size);
    if (tab.fontFace)
    {
        wxString cur = tab.fontFace->GetValue(); cur.Trim(true); cur.Trim(false);
        if (cur != resolvedFace) tab.fontFace->SetValue(resolvedFace);
    }

    if (tab.logTable) tab.logTable->Refresh();
}

void MainFrame::ApplyLogFontFromCurrentTab()
{
    auto* tab = CurrentTab();
    if (!tab) return;
    int      size = tab->fontSize ? tab->fontSize->GetValue() : 9;
    wxString face = tab->fontFace ? tab->fontFace->GetValue() : "";
    ApplyLogFont(*tab, size, face);
}

void MainFrame::OnMenuFontIncrease(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab) return;
    int size = tab->fontSize ? tab->fontSize->GetValue() : 9;
    ApplyLogFont(*tab, size + 1,
                 tab->fontFace ? tab->fontFace->GetValue() : "");
}

void MainFrame::OnMenuFontDecrease(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab) return;
    int size = tab->fontSize ? tab->fontSize->GetValue() : 9;
    ApplyLogFont(*tab, size - 1,
                 tab->fontFace ? tab->fontFace->GetValue() : "");
}

bool JournalApp::OnInit()
{
    wxApp::SetVendorName("DRSSH");
    wxApp::SetAppName("RemoteJournal");
    auto* frame=new MainFrame();
    frame->Show();
    return true;
}
