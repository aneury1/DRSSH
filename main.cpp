#include "RemoteJournal.h"
#include <iostream>

wxDEFINE_EVENT(EVT_SSH_LOG, wxThreadEvent);

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(wxID_HIGHEST + 1, MainFrame::OnConnect)
    EVT_BUTTON(wxID_HIGHEST + 2, MainFrame::OnDisconnect)
    EVT_BUTTON(wxID_HIGHEST + 3, MainFrame::OnSave)
    EVT_TOGGLEBUTTON(wxID_HIGHEST + 5, MainFrame::OnThemeToggle)
    EVT_BUTTON(wxID_HIGHEST + 6, MainFrame::OnApplyFilter)
    EVT_BUTTON(wxID_HIGHEST + 7, MainFrame::OnNewTab)
    EVT_BUTTON(wxID_HIGHEST + 8, MainFrame::OnUploadFile)
    EVT_BUTTON(wxID_HIGHEST + 9, MainFrame::OnCloseTab)
    EVT_BUTTON(wxID_HIGHEST + 10, MainFrame::OnExecuteCommand)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(JournalApp);

//////////////////////////////////////////////////////////////////
// SSHJournalReader
//////////////////////////////////////////////////////////////////

SSHJournalReader::SSHJournalReader()
    : m_session(nullptr),
      m_channel(nullptr),
      m_running(false)
{
}

SSHJournalReader::~SSHJournalReader()
{
    Stop();
}

bool SSHJournalReader::Connect(
    const std::string& host,
    const std::string& user,
    const std::string& password)
{
    Stop();

    m_session = ssh_new();
    if (!m_session)
        return false;

    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(m_session, SSH_OPTIONS_USER, user.c_str());

    if (ssh_connect(m_session) != SSH_OK)
    {
        ssh_free(m_session);
        m_session = nullptr;
        return false;
    }

    if (ssh_userauth_password(
            m_session,
            nullptr,
            password.c_str()) != SSH_AUTH_SUCCESS)
    {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
        return false;
    }

    return true;
}

bool SSHJournalReader::Start()
{
    if (m_running || !m_session)
        return false;

    m_channel = ssh_channel_new(m_session);
    if (!m_channel)
        return false;

    if (ssh_channel_open_session(m_channel) != SSH_OK ||
        ssh_channel_request_exec(
            m_channel,
            "journalctl -f -o short-iso") != SSH_OK)
    {
        ssh_channel_free(m_channel);
        m_channel = nullptr;
        return false;
    }

    m_running = true;
    m_thread = std::thread([this] { ReadLoop(); });
    return true;
}

void SSHJournalReader::SetCallback(LogCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = std::move(cb);
}

void SSHJournalReader::Stop()
{
    m_running = false;

    if (m_thread.joinable())
        m_thread.join();

    if (m_channel)
    {
        ssh_channel_send_eof(m_channel);
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }

    if (m_session)
    {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}

bool SSHJournalReader::IsRunning() const
{
    return m_running;
}

void SSHJournalReader::ReadLoop()
{
    std::array<char, 8192> buffer;
    std::string stdoutPending;
    std::string stderrPending;

    while (m_running)
    {
        int n = ssh_channel_read_timeout(
            m_channel,
            buffer.data(),
            buffer.size(),
            /*is_stderr=*/0,
            /*timeout_ms=*/200);

        if (n > 0)
        {
            DispatchBuffer(stdoutPending, buffer.data(), static_cast<std::size_t>(n));
        }
        else if (n < 0)
        {
            break;
        }

        int err = ssh_channel_read_timeout(
            m_channel,
            buffer.data(),
            buffer.size(),
            /*is_stderr=*/1,
            /*timeout_ms=*/0);

        if (err > 0)
        {
            DispatchBuffer(stderrPending, buffer.data(), static_cast<std::size_t>(err));
        }
        else if (err < 0)
        {
            break;
        }

        if (ssh_channel_is_eof(m_channel))
            break;
    }

    if (!stdoutPending.empty())
        DispatchLine(std::move(stdoutPending));

    if (!stderrPending.empty())
        DispatchLine(std::move(stderrPending));

    m_running = false;
}

void SSHJournalReader::DispatchBuffer(
    std::string& pending,
    const char* data,
    std::size_t size)
{
    pending.append(data, size);

    std::size_t lineStart = 0;
    for (;;)
    {
        const std::size_t newline = pending.find('\n', lineStart);
        if (newline == std::string::npos)
            break;

        DispatchLine(pending.substr(lineStart, newline - lineStart));
        lineStart = newline + 1;
    }

    pending.erase(0, lineStart);
}

void SSHJournalReader::DispatchLine(std::string line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    if (line.empty())
        return;

    std::cout << line << std::endl;

    LogCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_callback;
    }

    if (cb)
        cb(line);
}

//////////////////////////////////////////////////////////////////
// MainFrame
//////////////////////////////////////////////////////////////////

MainFrame::MainFrame()
    : wxFrame(
        nullptr,
        wxID_ANY,
        "Remote Journalctl Viewer",
        wxDefaultPosition,
        wxSize(1600, 900))
{
    CreateControls();

    Bind(EVT_SSH_LOG, &MainFrame::OnSSHLog, this);

    LoadSettings();

}

MainFrame::~MainFrame()
{
    SaveSettings();

    for (auto& tab : m_tabs)
    {
        if (tab->reader)
        {
            tab->reader->SetCallback(nullptr);
            tab->reader->Stop();
        }
    }
}

void MainFrame::CreateControls()
{
    auto* root =
        new wxBoxSizer(wxVERTICAL);

    root->SetMinSize(wxSize(980, 620));

    auto* actionBox =
        new wxStaticBoxSizer(wxHORIZONTAL, this, "Actions");

    auto* btns =
        new wxBoxSizer(wxHORIZONTAL);

    m_connect =
        new wxButton(this, wxID_HIGHEST + 1, "Connect");

    m_disconnect =
        new wxButton(this, wxID_HIGHEST + 2, "Disconnect");

    m_save =
        new wxButton(this, wxID_HIGHEST + 3, "Save Logs");

    m_applyFilter =
        new wxButton(this, wxID_HIGHEST + 6, "Apply Filter");

    m_newTab =
        new wxButton(this, wxID_HIGHEST + 7, "New Tab");

    m_closeTab =
        new wxButton(this, wxID_HIGHEST + 9, "Close Tab");

    m_uploadFile =
        new wxButton(this, wxID_HIGHEST + 8, "Upload File");

    m_executeCommand =
        new wxButton(this, wxID_HIGHEST + 10, "Run Command");

    m_darkTheme =
        new wxToggleButton(this, wxID_HIGHEST + 5, "Dark Mode");

    const wxSize buttonSize(118, 34);
    m_connect->SetMinSize(buttonSize);
    m_disconnect->SetMinSize(buttonSize);
    m_save->SetMinSize(buttonSize);
    m_applyFilter->SetMinSize(buttonSize);
    m_newTab->SetMinSize(buttonSize);
    m_closeTab->SetMinSize(buttonSize);
    m_uploadFile->SetMinSize(buttonSize);
    m_executeCommand->SetMinSize(buttonSize);
    m_darkTheme->SetMinSize(buttonSize);

    btns->Add(m_newTab,      0, wxRIGHT, 8);
    btns->Add(m_closeTab,    0, wxRIGHT, 8);
    btns->Add(m_connect,     0, wxRIGHT, 8);
    btns->Add(m_disconnect,  0, wxRIGHT, 8);
    btns->Add(m_applyFilter, 0, wxRIGHT, 8);
    btns->Add(m_executeCommand, 0, wxRIGHT, 8);
    btns->Add(m_uploadFile,  0, wxRIGHT, 8);
    btns->Add(m_save,        0, wxRIGHT, 8);
    btns->AddStretchSpacer();
    btns->Add(m_darkTheme);

    actionBox->Add(btns, 1, wxEXPAND | wxALL, 10);
    root->Add(actionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    m_notebook =
        new wxNotebook(this, wxID_ANY);

    root->Add(m_notebook, 1, wxEXPAND | wxALL, 10);

    SetSizer(root);
    CreateStatusBar();
    SetStatusText("Ready");
}

ConnectionTab* MainFrame::CurrentTab()
{
    return TabAt(CurrentTabIndex());
}

ConnectionTab* MainFrame::TabAt(std::size_t index)
{
    if (index >= m_tabs.size())
        return nullptr;

    return m_tabs[index].get();
}

std::size_t MainFrame::CurrentTabIndex() const
{
    if (!m_notebook || m_notebook->GetPageCount() == 0)
        return 0;

    const int selection = m_notebook->GetSelection();
    if (selection == wxNOT_FOUND)
        return 0;

    return static_cast<std::size_t>(selection);
}

ConnectionTab& MainFrame::AddConnectionTab(
    const wxString& title,
    bool select)
{
    auto tab =
        std::make_unique<ConnectionTab>();

    tab->page =
        new wxPanel(m_notebook, wxID_ANY);

    auto* root =
        new wxBoxSizer(wxVERTICAL);

    auto* connectionBox =
        new wxStaticBoxSizer(wxVERTICAL, tab->page, "Connection");

    auto* conn =
        new wxFlexGridSizer(2, 4, 8, 10);

    conn->Add(new wxStaticText(tab->page, wxID_ANY, "Host"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(tab->page, wxID_ANY, "User"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(tab->page, wxID_ANY, "Password"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(tab->page, wxID_ANY, "Regex Filter"), 0, wxALIGN_CENTER_VERTICAL);

    tab->host =
        new wxTextCtrl(tab->page, wxID_ANY);

    tab->user =
        new wxTextCtrl(tab->page, wxID_ANY);

    tab->password =
        new wxTextCtrl(
            tab->page,
            wxID_ANY,
            "",
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_PASSWORD);

    tab->filter =
        new wxTextCtrl(tab->page, wxID_ANY);

    tab->host->SetHint("server.example.com");
    tab->user->SetHint("root");
    tab->filter->SetHint("error|warning|sshd");

    conn->Add(tab->host,     1, wxEXPAND);
    conn->Add(tab->user,     1, wxEXPAND);
    conn->Add(tab->password, 1, wxEXPAND);
    conn->Add(tab->filter,   1, wxEXPAND);

    conn->AddGrowableCol(0);
    conn->AddGrowableCol(1);
    conn->AddGrowableCol(2);
    conn->AddGrowableCol(3);

    connectionBox->Add(conn, 0, wxEXPAND | wxALL, 10);
    root->Add(connectionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    tab->logView =
        new wxTextCtrl(
            tab->page,
            wxID_ANY,
            "",
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_MULTILINE |
            wxTE_READONLY  |
            wxTE_RICH2);

    tab->logView->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
    root->Add(tab->logView, 1, wxEXPAND | wxALL, 10);

    tab->page->SetSizer(root);
    tab->reader =
        std::make_unique<SSHJournalReader>();

    auto& ref = *tab;
    m_tabs.push_back(std::move(tab));
    m_notebook->AddPage(ref.page, title, select);

    return ref;
}

void MainFrame::UpdateTabTitle(std::size_t index)
{
    auto* tab = TabAt(index);
    if (!tab || !m_notebook)
        return;

    wxString label = tab->host->GetValue();
    if (label.empty())
        label = wxString::Format("Connection %zu", index + 1);

    m_notebook->SetPageText(index, label);
}

void MainFrame::CloseConnectionTab(std::size_t index)
{
    auto* tab = TabAt(index);
    if (!tab)
        return;

    if (m_tabs.size() == 1)
    {
        SetStatusText("At least one connection tab must remain");
        return;
    }

    if (tab->reader)
    {
        tab->reader->SetCallback(nullptr);
        tab->reader->Stop();
    }

    m_notebook->DeletePage(index);
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));

    const std::size_t nextIndex =
        index >= m_tabs.size() ? m_tabs.size() - 1 : index;

    if (!m_tabs.empty())
        m_notebook->SetSelection(nextIndex);

    SaveSettings();
    SetStatusText("Connection tab closed");
}

void MainFrame::LoadSettings()
{
    wxConfig config("RemoteJournal");

    long tabCount = 0;
    config.Read("tabs/count", &tabCount, 0);

    if (tabCount <= 0)
    {
        auto& tab = AddConnectionTab("Connection 1", true);

        wxString host;
        wxString user;
        wxString filter;

        if (config.Read("connection/host", &host))
            tab.host->SetValue(host);

        if (config.Read("connection/user", &user))
            tab.user->SetValue(user);

        if (config.Read("filter/pattern", &filter))
            tab.filter->SetValue(filter);

        if (!filter.empty())
        {
            tab.activeFilter = filter.ToStdString();
            try
            {
                tab.activeRegex = std::regex(tab.activeFilter);
            }
            catch (const std::regex_error&)
            {
                tab.activeFilter.clear();
                tab.activeRegex.reset();
                tab.filter->Clear();
            }
        }

        UpdateTabTitle(0);
    }
    else
    {
        for (long i = 0; i < tabCount; ++i)
        {
            auto& tab = AddConnectionTab(
                wxString::Format("Connection %ld", i + 1),
                i == 0);

            const wxString base =
                wxString::Format("tabs/%ld/", i);

            wxString host;
            wxString user;
            wxString filter;

            if (config.Read(base + "host", &host))
                tab.host->SetValue(host);

            if (config.Read(base + "user", &user))
                tab.user->SetValue(user);

            if (config.Read(base + "filter", &filter))
                tab.filter->SetValue(filter);

            if (!filter.empty())
            {
                tab.activeFilter = filter.ToStdString();
                try
                {
                    tab.activeRegex = std::regex(tab.activeFilter);
                }
                catch (const std::regex_error&)
                {
                    tab.activeFilter.clear();
                    tab.activeRegex.reset();
                    tab.filter->Clear();
                }
            }

            UpdateTabTitle(static_cast<std::size_t>(i));
        }
    }

    long selectedTab = 0;
    config.Read("tabs/selected", &selectedTab, 0);
    if (selectedTab >= 0 && selectedTab < static_cast<long>(m_tabs.size()))
        m_notebook->SetSelection(static_cast<std::size_t>(selectedTab));

    bool dark = false;
    config.Read("ui/darkTheme", &dark, false);
    m_darkEnabled = dark;
    m_darkTheme->SetValue(m_darkEnabled);
    ApplyTheme();

    long x = 0;
    long y = 0;
    long width = 1600;
    long height = 900;
    bool maximized = false;
    const bool hasGeometry =
        config.Read("window/x", &x) &&
        config.Read("window/y", &y) &&
        config.Read("window/width", &width) &&
        config.Read("window/height", &height);

    config.Read("window/maximized", &maximized, false);

    if (hasGeometry)
    {
        SetSize(
            static_cast<int>(x),
            static_cast<int>(y),
            static_cast<int>(width),
            static_cast<int>(height));
    }

    if (maximized)
        Maximize(true);
}

void MainFrame::SaveSettings()
{
    wxConfig config("RemoteJournal");

    config.DeleteGroup("tabs");
    config.Write("tabs/count", static_cast<long>(m_tabs.size()));
    config.Write("tabs/selected", static_cast<long>(CurrentTabIndex()));

    for (std::size_t i = 0; i < m_tabs.size(); ++i)
    {
        const auto& tab = *m_tabs[i];
        const wxString base =
            wxString::Format("tabs/%zu/", i);

        config.Write(base + "host", tab.host->GetValue());
        config.Write(base + "user", tab.user->GetValue());
        config.Write(base + "filter", tab.filter->GetValue());
    }

    config.Write("ui/darkTheme", m_darkEnabled);
    config.Write("window/maximized", IsMaximized());

    if (!IsMaximized())
    {
        const wxPoint pos = GetPosition();
        const wxSize size = GetSize();

        config.Write("window/x", static_cast<long>(pos.x));
        config.Write("window/y", static_cast<long>(pos.y));
        config.Write("window/width", static_cast<long>(size.GetWidth()));
        config.Write("window/height", static_cast<long>(size.GetHeight()));
    }

    config.Flush();
}

// Called only from the main thread (via OnSSHLog).
// Stores the line and conditionally shows it.
void MainFrame::AppendLog(
    std::size_t tabIndex,
    const std::string& line)
{
    auto* tab = TabAt(tabIndex);
    if (!tab)
        return;

    tab->logs.push_back(line);
    const bool show =
        !tab->activeRegex || std::regex_search(line, *tab->activeRegex);

    if (show)
    {
        tab->logView->AppendText(
            wxString::FromUTF8(line) + "\n");
        tab->logView->ShowPosition(tab->logView->GetLastPosition());
        tab->logView->Refresh();
    }
}

// Rebuilds the view from the stored log lines.
// Called only from the main thread.
void MainFrame::RefreshFilteredLogs(ConnectionTab& tab)
{
    tab.logView->Clear();

    // Freeze the control to avoid painting one line at
    // a time when re-populating with many entries.
    tab.logView->Freeze();

    for (const auto& line : tab.logs)
    {
        if (!tab.activeRegex || std::regex_search(line, *tab.activeRegex))
            tab.logView->AppendText(
                wxString::FromUTF8(line) + "\n");
    }

    tab.logView->ShowPosition(tab.logView->GetLastPosition());
    tab.logView->Thaw();
}

void MainFrame::OnConnect(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab || !tab->reader)
        return;

    SetStatusText("Connecting...");
    SaveSettings();
    const std::size_t tabIndex = CurrentTabIndex();

    // Set the callback BEFORE starting the reader so
    // that no lines are silently dropped between Start()
    // and the point where the callback would have been set.
    tab->reader->SetCallback(
        [this, tabIndex](const std::string& line)
        {
            // This lambda runs on the background thread.
            // Never touch wxWidgets objects here — only
            // queue a thread-safe event to the main thread.
            auto* evt = new wxThreadEvent(EVT_SSH_LOG);
            evt->SetInt(static_cast<int>(tabIndex));
            evt->SetString(wxString::FromUTF8(line));
            wxQueueEvent(this, evt);
        });

    bool ok =
        tab->reader->Connect(
            tab->host->GetValue().ToStdString(),
            tab->user->GetValue().ToStdString(),
            tab->password->GetValue().ToStdString());

    if (!ok)
    {
        // Clear the callback on failure so it does not
        // hold a dangling capture of `this`.
        tab->reader->SetCallback(nullptr);
        SetStatusText("Connection failed");
        wxMessageBox("SSH connection failed");
        return;
    }

    if (!tab->reader->Start())
    {
        tab->reader->SetCallback(nullptr);
        tab->reader->Stop();
        SetStatusText("Connected, but journalctl failed to start");
        wxMessageBox("Connected, but failed to start journalctl");
        return;
    }

    UpdateTabTitle(tabIndex);
    SetStatusText("Connected and streaming journalctl");
}

void MainFrame::OnDisconnect(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (tab && tab->reader)
    {
        tab->reader->SetCallback(nullptr);
        tab->reader->Stop();
    }

    SetStatusText("Disconnected");
}

void MainFrame::OnSave(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab)
        return;

    wxFileDialog dlg(
        this,
        "Save Logs",
        "",
        "",
        "*.txt",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dlg.ShowModal() != wxID_OK)
        return;

    // Logs are only ever touched from the main thread
    // so no locking is required here.
    std::ofstream out(dlg.GetPath().ToStdString());

    for (const auto& log : tab->logs)
        out << log << '\n';

    SetStatusText("Logs saved");
}

void MainFrame::OnApplyFilter(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab)
        return;

    const std::string filter =
        tab->filter->GetValue().ToStdString();

    if (filter.empty())
    {
        tab->activeFilter.clear();
        tab->activeRegex.reset();
        RefreshFilteredLogs(*tab);
        SaveSettings();
        SetStatusText("Filter cleared");
        return;
    }

    try
    {
        tab->activeRegex = std::regex(filter);
        tab->activeFilter = filter;
    }
    catch (const std::regex_error& e)
    {
        wxMessageBox(
            wxString::Format("Invalid regex:\n%s", e.what()),
            "Regex Error",
            wxOK | wxICON_ERROR,
            this);
        tab->filter->SetValue(wxString::FromUTF8(tab->activeFilter));
        return;
    }

    RefreshFilteredLogs(*tab);
    SaveSettings();
    SetStatusText("Filter applied");
}

void MainFrame::OnNewTab(wxCommandEvent&)
{
    AddConnectionTab(
        wxString::Format("Connection %zu", m_tabs.size() + 1),
        true);
    ApplyTheme();
    SaveSettings();
    SetStatusText("New connection tab created");
}

void MainFrame::OnCloseTab(wxCommandEvent&)
{
    CloseConnectionTab(CurrentTabIndex());
}

void MainFrame::OnUploadFile(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab)
        return;

    wxFileDialog fileDlg(
        this,
        "Select File to Upload",
        "",
        "",
        "*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (fileDlg.ShowModal() != wxID_OK)
        return;

    wxTextEntryDialog remoteDlg(
        this,
        "Remote directory to upload into",
        "SCP Upload",
        "/tmp");

    if (remoteDlg.ShowModal() != wxID_OK)
        return;

    SetStatusText("Uploading file...");

    if (UploadFileViaScp(*tab, fileDlg.GetPath(), remoteDlg.GetValue()))
        SetStatusText("File uploaded");
    else
        SetStatusText("File upload failed");
}

void MainFrame::OnExecuteCommand(wxCommandEvent&)
{
    auto* tab = CurrentTab();
    if (!tab)
        return;

    wxTextEntryDialog commandDlg(
        this,
        "Remote command to execute",
        "Run Remote Command",
        "uname -a");

    if (commandDlg.ShowModal() != wxID_OK)
        return;

    const wxString command = commandDlg.GetValue();
    if (command.empty())
        return;

    SetStatusText("Running remote command...");

    std::string output;
    if (!ExecuteRemoteCommand(*tab, command, output))
    {
        SetStatusText("Remote command failed");
        return;
    }

    wxString title = command;
    if (title.length() > 24)
        title = title.Left(21) + "...";

    auto& outputTab =
        AddConnectionTab("Cmd: " + title, true);

    outputTab.host->SetValue(tab->host->GetValue());
    outputTab.user->SetValue(tab->user->GetValue());

    const std::string header =
        "$ " + command.ToStdString() + "\n\n";
    outputTab.logs.push_back(header + output);
    outputTab.logView->SetValue(wxString::FromUTF8(header + output));
    outputTab.logView->ShowPosition(outputTab.logView->GetLastPosition());

    ApplyTheme();
    SaveSettings();
    SetStatusText("Remote command output opened in a new tab");
}

bool MainFrame::UploadFileViaScp(
    const ConnectionTab& tab,
    const wxString& localPath,
    const wxString& remoteDirectory)
{
    ssh_session session = ssh_new();
    if (!session)
    {
        wxMessageBox("Could not create SSH session");
        return false;
    }

    const std::string host =
        tab.host->GetValue().ToStdString();
    const std::string user =
        tab.user->GetValue().ToStdString();
    const std::string password =
        tab.password->GetValue().ToStdString();
    const std::string remoteDir =
        remoteDirectory.ToStdString();

    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());

    if (ssh_connect(session) != SSH_OK)
    {
        wxMessageBox(
            wxString::Format("SSH connection failed:\n%s", ssh_get_error(session)),
            "SCP Upload",
            wxOK | wxICON_ERROR,
            this);
        ssh_free(session);
        return false;
    }

    if (ssh_userauth_password(session, nullptr, password.c_str()) != SSH_AUTH_SUCCESS)
    {
        wxMessageBox(
            wxString::Format("SSH authentication failed:\n%s", ssh_get_error(session)),
            "SCP Upload",
            wxOK | wxICON_ERROR,
            this);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    wxFileName fileName(localPath);
    std::ifstream input(localPath.ToStdString(), std::ios::binary);
    if (!input)
    {
        wxMessageBox("Could not open local file");
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    input.seekg(0, std::ios::beg);

    if (fileSize < 0)
    {
        wxMessageBox("Could not determine local file size");
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

    ssh_scp scp =
        ssh_scp_new(session, SSH_SCP_WRITE, remoteDir.c_str());

    if (!scp)
    {
        wxMessageBox(
            wxString::Format("Could not create SCP session:\n%s", ssh_get_error(session)),
            "SCP Upload",
            wxOK | wxICON_ERROR,
            this);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    if (ssh_scp_init(scp) != SSH_OK)
    {
        wxMessageBox(
            wxString::Format("Could not initialize SCP upload:\n%s", ssh_get_error(session)),
            "SCP Upload",
            wxOK | wxICON_ERROR,
            this);
        ssh_scp_free(scp);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    const std::string remoteName =
        fileName.GetFullName().ToStdString();

    if (ssh_scp_push_file64(
            scp,
            remoteName.c_str(),
            static_cast<uint64_t>(fileSize),
            0644) != SSH_OK)
    {
        wxMessageBox(
            wxString::Format("Could not create remote file:\n%s", ssh_get_error(session)),
            "SCP Upload",
            wxOK | wxICON_ERROR,
            this);
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    std::array<char, 16384> buffer;
    while (input)
    {
        input.read(buffer.data(), buffer.size());
        const std::streamsize count = input.gcount();

        if (count <= 0)
            break;

        if (ssh_scp_write(scp, buffer.data(), static_cast<std::size_t>(count)) != SSH_OK)
        {
            wxMessageBox(
                wxString::Format("SCP write failed:\n%s", ssh_get_error(session)),
                "SCP Upload",
                wxOK | wxICON_ERROR,
                this);
            ssh_scp_close(scp);
            ssh_scp_free(scp);
            ssh_disconnect(session);
            ssh_free(session);
            return false;
        }
    }

    ssh_scp_close(scp);
    ssh_scp_free(scp);

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    ssh_disconnect(session);
    ssh_free(session);
    return true;
}

bool MainFrame::ExecuteRemoteCommand(
    const ConnectionTab& tab,
    const wxString& command,
    std::string& output)
{
    output.clear();

    ssh_session session = ssh_new();
    if (!session)
    {
        wxMessageBox("Could not create SSH session");
        return false;
    }

    const std::string host =
        tab.host->GetValue().ToStdString();
    const std::string user =
        tab.user->GetValue().ToStdString();
    const std::string password =
        tab.password->GetValue().ToStdString();
    const std::string cmd =
        command.ToStdString();

    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());

    if (ssh_connect(session) != SSH_OK)
    {
        wxMessageBox(
            wxString::Format("SSH connection failed:\n%s", ssh_get_error(session)),
            "Remote Command",
            wxOK | wxICON_ERROR,
            this);
        ssh_free(session);
        return false;
    }

    if (ssh_userauth_password(session, nullptr, password.c_str()) != SSH_AUTH_SUCCESS)
    {
        wxMessageBox(
            wxString::Format("SSH authentication failed:\n%s", ssh_get_error(session)),
            "Remote Command",
            wxOK | wxICON_ERROR,
            this);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    ssh_channel channel = ssh_channel_new(session);
    if (!channel)
    {
        wxMessageBox("Could not create SSH channel");
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    if (ssh_channel_open_session(channel) != SSH_OK ||
        ssh_channel_request_exec(channel, cmd.c_str()) != SSH_OK)
    {
        wxMessageBox(
            wxString::Format("Could not execute remote command:\n%s", ssh_get_error(session)),
            "Remote Command",
            wxOK | wxICON_ERROR,
            this);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    std::array<char, 8192> buffer;
    while (!ssh_channel_is_eof(channel))
    {
        int n = ssh_channel_read_timeout(
            channel,
            buffer.data(),
            buffer.size(),
            /*is_stderr=*/0,
            /*timeout_ms=*/200);

        if (n > 0)
            output.append(buffer.data(), static_cast<std::size_t>(n));
        else if (n < 0)
            break;

        int err = ssh_channel_read_timeout(
            channel,
            buffer.data(),
            buffer.size(),
            /*is_stderr=*/1,
            /*timeout_ms=*/0);

        if (err > 0)
            output.append(buffer.data(), static_cast<std::size_t>(err));
        else if (err < 0)
            break;
    }

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);
    return true;
}

void MainFrame::OnThemeToggle(wxCommandEvent&)
{
    m_darkEnabled = m_darkTheme->GetValue();
    ApplyTheme();
    SaveSettings();
}

void MainFrame::ApplyTheme()
{
    if (m_darkEnabled)
        ApplyDarkTheme(this);
    else
        ApplyLightTheme(this);

    m_darkTheme->SetLabel(m_darkEnabled ? "Light Mode" : "Dark Mode");
    SetStatusText(m_darkEnabled ? "Dark theme enabled" : "Light theme enabled");
    Layout();
    Refresh();
}

void MainFrame::ApplyDarkTheme(wxWindow* w)
{
    const wxColour frameBg(24, 26, 30);
    const wxColour panelBg(31, 34, 39);
    const wxColour textFg(232, 236, 241);
    const wxColour fieldBg(17, 19, 23);
    const wxColour accent(77, 144, 254);

    if (w == m_connect || w == m_applyFilter || w == m_uploadFile)
    {
        w->SetBackgroundColour(accent);
        w->SetForegroundColour(*wxWHITE);
    }
    else if (dynamic_cast<wxTextCtrl*>(w))
    {
        w->SetBackgroundColour(fieldBg);
        w->SetForegroundColour(textFg);
    }
    else if (dynamic_cast<wxButton*>(w) || dynamic_cast<wxToggleButton*>(w))
    {
        w->SetBackgroundColour(panelBg);
        w->SetForegroundColour(textFg);
    }
    else
    {
        w->SetBackgroundColour(frameBg);
        w->SetForegroundColour(textFg);
    }

    for (auto* child : w->GetChildren())
        ApplyDarkTheme(child);
}

void MainFrame::ApplyLightTheme(wxWindow* w)
{
    const wxColour frameBg(245, 247, 250);
    const wxColour fieldBg(255, 255, 255);
    const wxColour textFg(28, 32, 38);

    if (dynamic_cast<wxTextCtrl*>(w))
    {
        w->SetBackgroundColour(fieldBg);
        w->SetForegroundColour(textFg);
    }
    else
    {
        w->SetBackgroundColour(frameBg);
        w->SetForegroundColour(textFg);
    }

    for (auto* child : w->GetChildren())
        ApplyLightTheme(child);
}

void MainFrame::OnClose(wxCloseEvent& event)
{
    SaveSettings();
    event.Skip();
}

// Runs on the MAIN thread — safe to update the UI.
void MainFrame::OnSSHLog(wxThreadEvent& event)
{
    AppendLog(
        static_cast<std::size_t>(event.GetInt()),
        event.GetString().ToStdString());
}

//////////////////////////////////////////////////////////////////
// App
//////////////////////////////////////////////////////////////////

bool JournalApp::OnInit()
{
    SetVendorName("DRSSH");
    SetAppName("RemoteJournal");

    auto* frame = new MainFrame();
    frame->Show();
    return true;
}
