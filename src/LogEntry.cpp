#include "LogEntry.h"
#include <sstream>
#include <regex>

LogEntry LogEntry::Parse(const std::string& line)
{
    LogEntry e;
    e.raw = line;

    // journalctl short-iso:
    // 2024-05-10T12:34:56+0200 hostname service[pid]: message
    static const std::regex re(
        R"(^(\S+)\s+(\S+)\s+(\S+?)(?:\[(\d+)\])?:\s*(.*))");

    std::smatch m;
    if (std::regex_search(line, m, re))
    {
        e.timestamp = m[1].str();
        e.hostname  = m[2].str();
        e.service   = m[3].str();
        e.pid       = m[4].str();
        e.message   = m[5].str();
    }
    else
    {
        e.message = line;
    }

    return e;
}
