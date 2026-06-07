#pragma once
#include "LogEntry.h"
#include <string>
#include <vector>

class Database
{
public:
    Database();
    ~Database();

    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const;
    std::string FilePath() const;

    bool InsertLog(const std::string& sessionId,
                   const LogEntry&   entry);

    // Export session rows to plain text
    bool ExportText(const std::string& sessionId,
                    const std::string& outPath) const;

    // All session IDs present in this DB
    std::vector<std::string> QuerySessions() const;

    // All rows for one session
    std::vector<LogEntry> QueryLogs(const std::string& sessionId) const;

    // All rows regardless of session (for "open DB" viewer)
    std::vector<LogEntry> QueryAllLogs() const;

private:
    struct Impl;
    Impl* m_impl{nullptr};
};
