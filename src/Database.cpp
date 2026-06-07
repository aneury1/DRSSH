#include "Database.h"
#include <sqlite3.h>
#include <fstream>
#include <stdexcept>

struct Database::Impl
{
    sqlite3* db{nullptr};
};

Database::Database() : m_impl(new Impl) {}

Database::~Database()
{
    Close();
    delete m_impl;
}

bool Database::Open(const std::string& path)
{
    Close();
    int rc = sqlite3_open(path.c_str(), &m_impl->db);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
        return false;
    }

    // WAL for better write performance
    sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sql =
        "CREATE TABLE IF NOT EXISTS logs ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session   TEXT NOT NULL,"
        "  timestamp TEXT,"
        "  hostname  TEXT,"
        "  service   TEXT,"
        "  pid       TEXT,"
        "  message   TEXT,"
        "  raw       TEXT NOT NULL"
        ");";

    char* err = nullptr;
    rc = sqlite3_exec(m_impl->db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        sqlite3_free(err);
        Close();
        return false;
    }

    return true;
}

void Database::Close()
{
    if (m_impl->db)
    {
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
    }
}

bool Database::IsOpen() const
{
    return m_impl->db != nullptr;
}

bool Database::InsertLog(const std::string& sessionId,
                         const LogEntry&    entry)
{
    if (!m_impl->db) return false;

    const char* sql =
        "INSERT INTO logs(session,timestamp,hostname,service,pid,message,raw)"
        " VALUES(?,?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    auto bind = [&](int col, const std::string& val) {
        sqlite3_bind_text(stmt, col, val.c_str(), (int)val.size(), SQLITE_TRANSIENT);
    };

    bind(1, sessionId);
    bind(2, entry.timestamp);
    bind(3, entry.hostname);
    bind(4, entry.service);
    bind(5, entry.pid);
    bind(6, entry.message);
    bind(7, entry.raw);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::ExportText(const std::string& sessionId,
                          const std::string& outPath) const
{
    if (!m_impl->db) return false;

    const char* sql =
        "SELECT raw FROM logs WHERE session=? ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), (int)sessionId.size(), SQLITE_TRANSIENT);

    std::ofstream out(outPath);
    if (!out) { sqlite3_finalize(stmt); return false; }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (raw) out << raw << '\n';
    }

    sqlite3_finalize(stmt);
    return true;
}

std::vector<LogEntry> Database::QueryLogs(const std::string& sessionId) const
{
    std::vector<LogEntry> result;
    if (!m_impl->db) return result;

    const char* sql =
        "SELECT timestamp,hostname,service,pid,message,raw"
        " FROM logs WHERE session=? ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), (int)sessionId.size(), SQLITE_TRANSIENT);

    auto col = [&](int c) -> std::string {
        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
        return t ? t : "";
    };

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        LogEntry e;
        e.timestamp = col(0);
        e.hostname  = col(1);
        e.service   = col(2);
        e.pid       = col(3);
        e.message   = col(4);
        e.raw       = col(5);
        result.push_back(std::move(e));
    }

    sqlite3_finalize(stmt);
    return result;
}
