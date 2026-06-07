#pragma once
#include <string>
#include <vector>

struct ConnectionProfile
{
    std::string name;
    std::string host;
    int         port{22};
    std::string user;
    std::string password;   // stored plain (user's choice)
    std::string filter;
    std::string journalArgs; // extra args for journalctl
};

// Persist profiles to/from wxConfig
class ProfileManager
{
public:
    void Load();
    void Save() const;

    std::vector<ConnectionProfile>& Profiles()       { return m_profiles; }
    const std::vector<ConnectionProfile>& Profiles() const { return m_profiles; }

    void Add(const ConnectionProfile& p);
    void Remove(std::size_t index);
    void Update(std::size_t index, const ConnectionProfile& p);

private:
    std::vector<ConnectionProfile> m_profiles;
};
