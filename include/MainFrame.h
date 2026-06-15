#pragma once
#include "Events.h"
#include "ConnectionTab.h"
#include "ConnectionProfile.h"
#include "FilterConfig.h"
#include "AutoStart.h"

#include <wx/frame.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/menu.h>
#include <wx/statusbr.h>

#include <vector>
#include <memory>
#include <cstddef>

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame();

    void ApplyFilterConfigToAllTabs();

private:
    void BuildMenu();
    void CreateControls();
    void RebuildProfileMenu();

    // Left-panel sidebar (connection + filter controls)
    wxPanel* BuildSidebar(wxWindow* parent, ConnectionTab& tab);

    ConnectionTab& AddConnectionTab(const wxString& title, bool select);
    void           UpdateTabTitle(std::size_t index);
    void           CloseConnectionTab(std::size_t index);
    ConnectionTab* CurrentTab();
    ConnectionTab* TabAt(std::size_t index);
    std::size_t    CurrentTabIndex() const;

    void AppendLog(std::size_t tabIndex, const std::string& line);

    void LoadSettings();
    void SaveSettings();

    void ApplyTheme();
    void ApplyDarkTheme(wxWindow*);
    void ApplyLightTheme(wxWindow*);

    bool DoConnect(ConnectionTab& tab, std::size_t tabIndex);
    void DoDisconnect(ConnectionTab& tab);
    void SaveLogsToFile(ConnectionTab& tab);
    void OpenDatabaseInTab(const wxString& path);
    void OpenTextFileInTab(const wxString& path);
    void ExportVisibleRows(ConnectionTab& tab);

    // AutoStart
    void LoadAutoStart();
    void SaveAutoStart();
    void OnMenuAutoStartSettings(wxCommandEvent&);

    // Menu handlers
    void OnMenuNewTab(wxCommandEvent&);
    void OnMenuCloseTab(wxCommandEvent&);
    void OnMenuSave(wxCommandEvent&);
    void OnMenuOpenTxt(wxCommandEvent&);
    void OnMenuExportVisible(wxCommandEvent&);
    void OnMenuExportJsonVisible(wxCommandEvent&);
    void OnMenuExportJsonAll(wxCommandEvent&);
    void OnMenuImportJson(wxCommandEvent&);

    // JSON helpers
    bool ExportLogsToJson(const std::vector<LogEntry>& entries,
                          const wxString& path) const;
    bool ImportLogsFromJson(const wxString& path, ConnectionTab& tab);
    void OnMenuSaveDb(wxCommandEvent&);
    void OnMenuOpenDb(wxCommandEvent&);
    void OnMenuConnect(wxCommandEvent&);
    void OnMenuDisconnect(wxCommandEvent&);
    void OnMenuManageProfiles(wxCommandEvent&);
    void OnMenuLoadProfile(wxCommandEvent&);
    void OnMenuApplyFilter(wxCommandEvent&);
    void OnMenuClearFilter(wxCommandEvent&);
    void OnMenuUploadFile(wxCommandEvent&);
    void OnMenuExecuteCommand(wxCommandEvent&);
    void OnMenuDarkTheme(wxCommandEvent&);
    void OnMenuFontIncrease(wxCommandEvent&);
    void OnMenuFontDecrease(wxCommandEvent&);
    void ApplyLogFontFromCurrentTab();
    void ApplyLogFont(ConnectionTab& tab, int size, const wxString& face);
    void OnMenuFilterConfig(wxCommandEvent&);
    void OnMenuLoadFilterJson(wxCommandEvent&);
    void OnMenuSaveFilterJson(wxCommandEvent&);

    void OnSSHLog(wxThreadEvent&);
    void OnSSHStatus(wxThreadEvent&);
    void OnNotebookPageChanged(wxNotebookEvent&);
    void OnClose(wxCloseEvent&);

private:
    wxNotebook*    m_notebook{};
    wxMenuBar*     m_menuBar{};
    wxMenu*        m_profileMenu{};

    std::vector<std::unique_ptr<ConnectionTab>> m_tabs;
    ProfileManager  m_profiles;
    FilterConfig    m_filterConfig;
    AutoStartConfig m_autoStart;
    std::string     m_autoStartKey{"RemoteJournal2024"}; // default enc key
    bool            m_darkEnabled{false};

    enum {
        ID_MENU_NEW_TAB = wxID_HIGHEST + 100,
        ID_MENU_CLOSE_TAB,
        ID_MENU_SAVE,
        ID_MENU_SAVE_DB,
        ID_MENU_OPEN_DB,
        ID_MENU_CONNECT,
        ID_MENU_DISCONNECT,
        ID_MENU_MANAGE_PROFILES,
        ID_MENU_AUTOSTART,
        ID_MENU_LOAD_PROFILE_BASE,
        ID_MENU_APPLY_FILTER = ID_MENU_LOAD_PROFILE_BASE + 50,
        ID_MENU_CLEAR_FILTER,
        ID_MENU_UPLOAD_FILE,
        ID_MENU_EXECUTE_COMMAND,
        ID_MENU_DARK_THEME,
        ID_MENU_FILTER_CONFIG,
        ID_MENU_LOAD_FILTER_JSON,
        ID_MENU_SAVE_FILTER_JSON,
        ID_MENU_FONT_INC,
        ID_MENU_FONT_DEC,
        ID_MENU_OPEN_TXT,
        ID_MENU_EXPORT_VISIBLE,
        // JSON export/import
        ID_MENU_EXPORT_JSON_VISIBLE,
        ID_MENU_EXPORT_JSON_ALL,
        ID_MENU_IMPORT_JSON,
    };

    wxDECLARE_EVENT_TABLE();
};

class JournalApp : public wxApp
{
public:
    bool OnInit() override;
};
