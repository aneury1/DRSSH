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
#include <wx/notebook.h>

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

struct ConnectionTab
{
    wxPanel* page{};
    wxTextCtrl* host{};
    wxTextCtrl* user{};
    wxTextCtrl* password{};
    wxTextCtrl* filter{};
    wxTextCtrl* logView{};

    std::vector<std::string> logs;
    std::string activeFilter;
    std::optional<std::regex> activeRegex;
    std::unique_ptr<SSHJournalReader> reader;
};

class MainFrame : public wxFrame
{
public:

    MainFrame();

    ~MainFrame();

private:

    void CreateControls();

    ConnectionTab* CurrentTab();

    ConnectionTab* TabAt(
        std::size_t index);

    std::size_t CurrentTabIndex() const;

    ConnectionTab& AddConnectionTab(
        const wxString& title,
        bool select);

    void UpdateTabTitle(
        std::size_t index);

    void CloseConnectionTab(
        std::size_t index);

    // Must be called only from the main thread.
    // Appends one line to m_logs and conditionally
    // appends it to m_logView based on the current filter.
    void AppendLog(
        std::size_t tabIndex,
        const std::string& line);

    // Rebuilds m_logView from m_logs applying the
    // current filter.  Must be called from main thread.
    void RefreshFilteredLogs(
        ConnectionTab& tab);

    void LoadSettings();

    void SaveSettings();

    void ApplyTheme();

    void ApplyDarkTheme(
        wxWindow* window);

    void ApplyLightTheme(
        wxWindow* window);

    bool UploadFileViaScp(
        const ConnectionTab& tab,
        const wxString& localPath,
        const wxString& remoteDirectory);

    bool ExecuteRemoteCommand(
        const ConnectionTab& tab,
        const wxString& command,
        std::string& output);

    void OnConnect(
        wxCommandEvent&);

    void OnDisconnect(
        wxCommandEvent&);

    void OnSave(
        wxCommandEvent&);

    void OnApplyFilter(
        wxCommandEvent&);

    void OnNewTab(
        wxCommandEvent&);

    void OnCloseTab(
        wxCommandEvent&);

    void OnUploadFile(
        wxCommandEvent&);

    void OnExecuteCommand(
        wxCommandEvent&);

    void OnThemeToggle(
        wxCommandEvent&);

    void OnClose(
        wxCloseEvent&);

    void OnSSHLog(
        wxThreadEvent&);

private:

    wxNotebook* m_notebook{};

    wxButton* m_connect{};
    wxButton* m_disconnect{};
    wxButton* m_save{};
    wxButton* m_applyFilter{};
    wxButton* m_newTab{};
    wxButton* m_closeTab{};
    wxButton* m_uploadFile{};
    wxButton* m_executeCommand{};

    wxToggleButton* m_darkTheme{};

    std::vector<std::unique_ptr<ConnectionTab>> m_tabs;

    bool m_darkEnabled{false};

    wxDECLARE_EVENT_TABLE();
};

class JournalApp : public wxApp
{
public:

    bool OnInit() override;
};
