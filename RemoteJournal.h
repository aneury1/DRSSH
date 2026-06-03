#pragma once

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/filedlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/config.h>
#include <wx/statbox.h>
#include <wx/statline.h>

#include <libssh/libssh.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <string>
#include <regex>
#include <functional>
#include <fstream>
#include <sstream>
#include <array>
#include <optional>

wxDECLARE_EVENT(EVT_SSH_LOG, wxThreadEvent);

class SSHJournalReader
{
public:

    using LogCallback =
        std::function<void(const std::string&)>;

    SSHJournalReader();
    ~SSHJournalReader();

    bool Connect(
        const std::string& host,
        const std::string& user,
        const std::string& password);

    bool Start();

    void Stop();

    bool IsRunning() const;

    void SetCallback(LogCallback cb);

private:

    void ReadLoop();

    void DispatchBuffer(
        std::string& pending,
        const char* data,
        std::size_t size);

    void DispatchLine(
        std::string line);

private:

    ssh_session m_session;
    ssh_channel m_channel;

    std::atomic_bool m_running;

    std::thread m_thread;

    // Protects m_callback
    std::mutex m_callbackMutex;
    LogCallback m_callback;
};

class MainFrame : public wxFrame
{
public:

    MainFrame();

    ~MainFrame();

private:

    void CreateControls();

    // Must be called only from the main thread.
    // Appends one line to m_logs and conditionally
    // appends it to m_logView based on the current filter.
    void AppendLog(
        const std::string& line);

    // Rebuilds m_logView from m_logs applying the
    // current filter.  Must be called from main thread.
    void RefreshFilteredLogs();

    void LoadSettings();

    void SaveSettings();

    void ApplyTheme();

    void ApplyDarkTheme(
        wxWindow* window);

    void ApplyLightTheme(
        wxWindow* window);

    void OnConnect(
        wxCommandEvent&);

    void OnDisconnect(
        wxCommandEvent&);

    void OnSave(
        wxCommandEvent&);

    void OnApplyFilter(
        wxCommandEvent&);

    void OnThemeToggle(
        wxCommandEvent&);

    void OnClose(
        wxCloseEvent&);

    void OnSSHLog(
        wxThreadEvent&);

private:

    wxTextCtrl* m_host{};
    wxTextCtrl* m_user{};
    wxTextCtrl* m_password{};
    wxTextCtrl* m_filter{};

    wxButton* m_connect{};
    wxButton* m_disconnect{};
    wxButton* m_save{};
    wxButton* m_applyFilter{};

    wxToggleButton* m_darkTheme{};

    wxTextCtrl* m_logView{};

    // Accessed only from the main thread —
    // no mutex needed here.
    std::vector<std::string> m_logs;
    std::string m_activeFilter;
    std::optional<std::regex> m_activeRegex;

    bool m_darkEnabled{false};

    std::unique_ptr<SSHJournalReader> m_reader;

    wxDECLARE_EVENT_TABLE();
};

class JournalApp : public wxApp
{
public:

    bool OnInit() override;
};
