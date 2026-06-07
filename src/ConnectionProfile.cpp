#include "ConnectionProfile.h"
#include <wx/config.h>

void ProfileManager::Load()
{
    m_profiles.clear();
    wxConfig cfg("RemoteJournal");

    long count = 0;
    cfg.Read("profiles/count", &count, 0);

    for (long i = 0; i < count; ++i)
    {
        wxString base = wxString::Format("profiles/%ld/", i);
        ConnectionProfile p;

        wxString tmp;
        cfg.Read(base + "name",        &tmp); p.name        = tmp.ToStdString();
        cfg.Read(base + "host",        &tmp); p.host        = tmp.ToStdString();
        cfg.Read(base + "user",        &tmp); p.user        = tmp.ToStdString();
        cfg.Read(base + "password",    &tmp); p.password    = tmp.ToStdString();
        cfg.Read(base + "filter",      &tmp); p.filter      = tmp.ToStdString();
        cfg.Read(base + "journalArgs", &tmp); p.journalArgs = tmp.ToStdString();

        long port = 22;
        cfg.Read(base + "port", &port, 22);
        p.port = (int)port;

        m_profiles.push_back(std::move(p));
    }
}

void ProfileManager::Save() const
{
    wxConfig cfg("RemoteJournal");
    cfg.DeleteGroup("profiles");
    cfg.Write("profiles/count", (long)m_profiles.size());

    for (std::size_t i = 0; i < m_profiles.size(); ++i)
    {
        const auto& p = m_profiles[i];
        wxString base = wxString::Format("profiles/%zu/", i);
        cfg.Write(base + "name",        wxString::FromUTF8(p.name));
        cfg.Write(base + "host",        wxString::FromUTF8(p.host));
        cfg.Write(base + "port",        (long)p.port);
        cfg.Write(base + "user",        wxString::FromUTF8(p.user));
        cfg.Write(base + "password",    wxString::FromUTF8(p.password));
        cfg.Write(base + "filter",      wxString::FromUTF8(p.filter));
        cfg.Write(base + "journalArgs", wxString::FromUTF8(p.journalArgs));
    }

    cfg.Flush();
}

void ProfileManager::Add(const ConnectionProfile& p)
{
    m_profiles.push_back(p);
}

void ProfileManager::Remove(std::size_t index)
{
    if (index < m_profiles.size())
        m_profiles.erase(m_profiles.begin() + (std::ptrdiff_t)index);
}

void ProfileManager::Update(std::size_t index, const ConnectionProfile& p)
{
    if (index < m_profiles.size())
        m_profiles[index] = p;
}
