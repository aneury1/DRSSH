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

    bool InsertLog(const std::string& sessionId,
                   const LogEntry&   entry);

    bool ExportText(const std::string& sessionId,
                    const std::string& outPath) const;

    std::vector<LogEntry> QueryLogs(const std::string& sessionId) const;

private:
    struct Impl;
    Impl* m_impl{nullptr};
};
