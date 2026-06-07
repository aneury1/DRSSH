#pragma once
#include <string>
#include <vector>

// One session entry stored in autostart.json (AES-256-CBC encrypted)
struct AutoStartSession
{
    std::string profileName;   // display name
    std::string host;
    int         port{22};
    std::string user;
    std::string encryptedPass; // base64(AES-CBC(password))
    std::string journalArgs;
    std::string filter;
    std::string negFilter;     // negative filter regex
    bool        useSqlite{false};
    std::string dbPath;
    bool        autoConnect{false}; // reconnect at launch
};

class AutoStartConfig
{
public:
    // key = 32-byte passphrase (padded/truncated internally)
    bool Load(const std::string& path, const std::string& key);
    bool Save(const std::string& path, const std::string& key) const;

    std::vector<AutoStartSession>& Sessions()             { return m_sessions; }
    const std::vector<AutoStartSession>& Sessions() const { return m_sessions; }

    // Encrypt / decrypt a single string (base64 AES-CBC)
    static std::string Encrypt(const std::string& plain, const std::string& key);
    static std::string Decrypt(const std::string& cipher64, const std::string& key);

private:
    std::vector<AutoStartSession> m_sessions;
};
