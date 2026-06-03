#include "RemoteJournal.h"
#include <iostream>

wxDEFINE_EVENT(EVT_SSH_LOG, wxThreadEvent);

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(wxID_HIGHEST + 1, MainFrame::OnConnect)
    EVT_BUTTON(wxID_HIGHEST + 2, MainFrame::OnDisconnect)
    EVT_BUTTON(wxID_HIGHEST + 3, MainFrame::OnSave)
    EVT_TOGGLEBUTTON(wxID_HIGHEST + 5, MainFrame::OnThemeToggle)
    EVT_BUTTON(wxID_HIGHEST + 6, MainFrame::OnApplyFilter)
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

    m_reader =
        std::make_unique<SSHJournalReader>();
}

MainFrame::~MainFrame()
{
    SaveSettings();

    if (m_reader)
    {
        m_reader->SetCallback(nullptr);
        m_reader->Stop();
    }
}

void MainFrame::CreateControls()
{
    auto* root =
        new wxBoxSizer(wxVERTICAL);

    root->SetMinSize(wxSize(980, 620));

    auto* connectionBox =
        new wxStaticBoxSizer(wxVERTICAL, this, "Connection");

    auto* conn =
        new wxFlexGridSizer(2, 4, 8, 10);

    conn->Add(new wxStaticText(this, wxID_ANY, "Host"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(this, wxID_ANY, "User"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(this, wxID_ANY, "Password"), 0, wxALIGN_CENTER_VERTICAL);
    conn->Add(new wxStaticText(this, wxID_ANY, "Regex Filter"), 0, wxALIGN_CENTER_VERTICAL);

    m_host =
        new wxTextCtrl(this, wxID_ANY);

    m_user =
        new wxTextCtrl(this, wxID_ANY);

    m_password =
        new wxTextCtrl(
            this,
            wxID_ANY,
            "",
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_PASSWORD);

    m_filter =
        new wxTextCtrl(this, wxID_HIGHEST + 4);

    m_host->SetHint("server.example.com");
    m_user->SetHint("root");
    m_filter->SetHint("error|warning|sshd");

    conn->Add(m_host,     1, wxEXPAND);
    conn->Add(m_user,     1, wxEXPAND);
    conn->Add(m_password, 1, wxEXPAND);
    conn->Add(m_filter,   1, wxEXPAND);

    conn->AddGrowableCol(0);
    conn->AddGrowableCol(1);
    conn->AddGrowableCol(2);
    conn->AddGrowableCol(3);

    connectionBox->Add(conn, 0, wxEXPAND | wxALL, 10);
    root->Add(connectionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

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

    m_darkTheme =
        new wxToggleButton(this, wxID_HIGHEST + 5, "Dark Mode");

    const wxSize buttonSize(128, 34);
    m_connect->SetMinSize(buttonSize);
    m_disconnect->SetMinSize(buttonSize);
    m_save->SetMinSize(buttonSize);
    m_applyFilter->SetMinSize(buttonSize);
    m_darkTheme->SetMinSize(buttonSize);

    btns->Add(m_connect,     0, wxRIGHT, 8);
    btns->Add(m_disconnect,  0, wxRIGHT, 8);
    btns->Add(m_applyFilter, 0, wxRIGHT, 8);
    btns->Add(m_save,        0, wxRIGHT, 8);
    btns->AddStretchSpacer();
    btns->Add(m_darkTheme);

    actionBox->Add(btns, 1, wxEXPAND | wxALL, 10);
    root->Add(actionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    m_logView =
        new wxTextCtrl(
            this,
            wxID_ANY,
            "",
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_MULTILINE |
            wxTE_READONLY  |
            wxTE_RICH2);

    m_logView->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));

    root->Add(m_logView, 1, wxEXPAND | wxALL, 10);

    SetSizer(root);
    CreateStatusBar();
    SetStatusText("Ready");
}

void MainFrame::LoadSettings()
{
    wxConfig config("RemoteJournal");

    wxString host;
    wxString user;
    wxString filter;

    if (config.Read("connection/host", &host))
        m_host->SetValue(host);

    if (config.Read("connection/user", &user))
        m_user->SetValue(user);

    if (config.Read("filter/pattern", &filter))
    {
        m_filter->SetValue(filter);
        m_activeFilter = filter.ToStdString();

        if (!m_activeFilter.empty())
        {
            try
            {
                m_activeRegex = std::regex(m_activeFilter);
            }
            catch (const std::regex_error&)
            {
                m_activeFilter.clear();
                m_activeRegex.reset();
                m_filter->Clear();
            }
        }
    }

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

    config.Write("connection/host", m_host->GetValue());
    config.Write("connection/user", m_user->GetValue());
    config.Write("filter/pattern", m_filter->GetValue());
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
void MainFrame::AppendLog(const std::string& line)
{
    m_logs.push_back(line);

    const bool show =
        !m_activeRegex || std::regex_search(line, *m_activeRegex);

    if (show)
    {
        m_logView->AppendText(
            wxString::FromUTF8(line) + "\n");
        m_logView->ShowPosition(m_logView->GetLastPosition());
        m_logView->Refresh();
    }
}

// Rebuilds the view from the stored log lines.
// Called only from the main thread.
void MainFrame::RefreshFilteredLogs()
{
    m_logView->Clear();

    // Freeze the control to avoid painting one line at
    // a time when re-populating with many entries.
    m_logView->Freeze();

    for (const auto& line : m_logs)
    {
        if (!m_activeRegex || std::regex_search(line, *m_activeRegex))
            m_logView->AppendText(
                wxString::FromUTF8(line) + "\n");
    }

    m_logView->ShowPosition(m_logView->GetLastPosition());
    m_logView->Thaw();
}

void MainFrame::OnConnect(wxCommandEvent&)
{
    if (!m_reader)
        return;

    SetStatusText("Connecting...");
    SaveSettings();

    // Set the callback BEFORE starting the reader so
    // that no lines are silently dropped between Start()
    // and the point where the callback would have been set.
    m_reader->SetCallback(
        [this](const std::string& line)
        {
            // This lambda runs on the background thread.
            // Never touch wxWidgets objects here — only
            // queue a thread-safe event to the main thread.
            auto* evt = new wxThreadEvent(EVT_SSH_LOG);
            evt->SetString(wxString::FromUTF8(line));
            wxQueueEvent(this, evt);
        });

    bool ok =
        m_reader->Connect(
            m_host->GetValue().ToStdString(),
            m_user->GetValue().ToStdString(),
            m_password->GetValue().ToStdString());

    if (!ok)
    {
        // Clear the callback on failure so it does not
        // hold a dangling capture of `this`.
        m_reader->SetCallback(nullptr);
        SetStatusText("Connection failed");
        wxMessageBox("SSH connection failed");
        return;
    }

    if (!m_reader->Start())
    {
        m_reader->SetCallback(nullptr);
        m_reader->Stop();
        SetStatusText("Connected, but journalctl failed to start");
        wxMessageBox("Connected, but failed to start journalctl");
        return;
    }

    SetStatusText("Connected and streaming journalctl");
}

void MainFrame::OnDisconnect(wxCommandEvent&)
{
    if (m_reader)
    {
        m_reader->SetCallback(nullptr);
        m_reader->Stop();
    }

    SetStatusText("Disconnected");
}

void MainFrame::OnSave(wxCommandEvent&)
{
    wxFileDialog dlg(
        this,
        "Save Logs",
        "",
        "",
        "*.txt",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dlg.ShowModal() != wxID_OK)
        return;

    // m_logs is only ever touched from the main thread
    // so no locking is required here.
    std::ofstream out(dlg.GetPath().ToStdString());

    for (const auto& log : m_logs)
        out << log << '\n';

    SetStatusText("Logs saved");
}

void MainFrame::OnApplyFilter(wxCommandEvent&)
{
    const std::string filter =
        m_filter->GetValue().ToStdString();

    if (filter.empty())
    {
        m_activeFilter.clear();
        m_activeRegex.reset();
        RefreshFilteredLogs();
        SaveSettings();
        SetStatusText("Filter cleared");
        return;
    }

    try
    {
        m_activeRegex = std::regex(filter);
        m_activeFilter = filter;
    }
    catch (const std::regex_error& e)
    {
        wxMessageBox(
            wxString::Format("Invalid regex:\n%s", e.what()),
            "Regex Error",
            wxOK | wxICON_ERROR,
            this);
        m_filter->SetValue(wxString::FromUTF8(m_activeFilter));
        return;
    }

    RefreshFilteredLogs();
    SaveSettings();
    SetStatusText("Filter applied");
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

    if (w == m_connect || w == m_applyFilter)
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
    AppendLog(event.GetString().ToStdString());
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
