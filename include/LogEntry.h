#pragma once
#include <string>

struct LogEntry
{
    std::string timestamp;
    std::string hostname;
    std::string service;
    std::string pid;
    std::string message;
    std::string raw;

    static LogEntry Parse(const std::string& line);
};
