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
#include <optional>
#include <regex>

struct ConnectionTab
{
    // UI (owned by notebook page)
    wxPanel*       page{};
    wxTextCtrl*    host{};
    wxSpinCtrl*    port{};
    wxTextCtrl*    user{};
    wxTextCtrl*    password{};
    wxTextCtrl*    filter{};       // positive regex filter
    wxTextCtrl*    negFilter{};    // negative regex filter (exclude matches)
    wxTextCtrl*    journalArgs{};
    wxCheckBox*    useSqlite{};
    wxTextCtrl*    dbPath{};
    LogTableCtrl*  logTable{};
    wxTextCtrl*    payloadCtrl{};
    wxSpinCtrl*    fontSize{};     // log font size control
    wxTextCtrl*    fontFace{};     // log font face control

    // Runtime
    std::unique_ptr<SSHJournalReader> reader;
    std::unique_ptr<Database>         db;
    std::string                       sessionId;

    // Active filters
    std::string               activeFilter;
    std::string               activeNegFilter;
    std::optional<std::regex> activeRegex;
    std::optional<std::regex> activeNegRegex;

    int profileIndex{-1};
};
