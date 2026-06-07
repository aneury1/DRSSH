#pragma once
#include "SSHJournalReader.h"
#include "LogTableCtrl.h"
#include "Database.h"
#include "ConnectionProfile.h"

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <regex>

struct ConnectionTab
{
    // UI widgets (all owned by the notebook page)
    wxPanel*       page{};
    wxTextCtrl*    host{};
    wxSpinCtrl*    port{};
    wxTextCtrl*    user{};
    wxTextCtrl*    password{};
    wxTextCtrl*    filter{};
    wxTextCtrl*    journalArgs{};
    wxCheckBox*    useSqlite{};
    wxTextCtrl*    dbPath{};
    LogTableCtrl*  logTable{};
    wxTextCtrl*    payloadCtrl{};

    // Runtime
    std::unique_ptr<SSHJournalReader> reader;
    std::unique_ptr<Database>         db;
    std::string                       sessionId;

    // Active filter
    std::string              activeFilter;
    std::optional<std::regex> activeRegex;

    // Profile this tab was loaded from (-1 = none)
    int profileIndex{-1};
};
