#include <fstream>
#include <array>
#include "SSHJournalReader.h"
#include <iostream>

SSHJournalReader::SSHJournalReader() = default;

SSHJournalReader::~SSHJournalReader()
{
    Stop();
}

bool SSHJournalReader::Connect(const std::string& host,
                                int                port,
                                const std::string& user,
                                const std::string& password)
{
    Stop();

    m_session = ssh_new();
    if (!m_session) return false;

    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(m_session, SSH_OPTIONS_USER, user.c_str());

    if (ssh_connect(m_session) != SSH_OK)
    {
        ssh_free(m_session);
        m_session = nullptr;
        return false;
    }

    if (ssh_userauth_password(m_session, nullptr, password.c_str()) != SSH_AUTH_SUCCESS)
    {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
        return false;
    }

    return true;
}

bool SSHJournalReader::Start(const std::string& command)
{
    if (m_running || !m_session) return false;

    m_channel = ssh_channel_new(m_session);
    if (!m_channel) return false;

    if (ssh_channel_open_session(m_channel) != SSH_OK ||
        ssh_channel_request_exec(m_channel, command.c_str()) != SSH_OK)
    {
        ssh_channel_free(m_channel);
        m_channel = nullptr;
        return false;
    }

    m_running = true;
    m_thread  = std::thread([this] { ReadLoop(); });
    return true;
}

void SSHJournalReader::Stop()
{
    m_running = false;

    if (m_thread.joinable())
        m_thread.join();

    if (m_channel)
    {
        ssh_channel_send_eof(m_channel);
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }

    if (m_session)
    {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}

bool SSHJournalReader::IsRunning() const { return m_running; }

void SSHJournalReader::SetCallback(LogCallback cb)
{
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    m_callback = std::move(cb);
}

bool SSHJournalReader::UploadFile(const std::string& localPath,
                                   const std::string& remoteDirectory)
{
    if (!m_session) return false;

    std::ifstream fin(localPath, std::ios::binary);
    if (!fin) return false;

    fin.seekg(0, std::ios::end);
    auto sz = fin.tellg();
    fin.seekg(0, std::ios::beg);
    if (sz < 0) return false;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    ssh_scp scp = ssh_scp_new(m_session, SSH_SCP_WRITE, remoteDirectory.c_str());
    if (!scp) return false;

    if (ssh_scp_init(scp) != SSH_OK)
    {
        ssh_scp_free(scp);
        return false;
    }

    // filename from path
    std::string fname = localPath;
    auto slash = fname.rfind('/');
    if (slash != std::string::npos) fname = fname.substr(slash + 1);

    if (ssh_scp_push_file64(scp, fname.c_str(), (uint64_t)sz, 0644) != SSH_OK)
    {
        ssh_scp_close(scp); ssh_scp_free(scp);
        return false;
    }

    std::array<char, 16384> buf;
    while (fin)
    {
        fin.read(buf.data(), buf.size());
        std::streamsize n = fin.gcount();
        if (n <= 0) break;
        if (ssh_scp_write(scp, buf.data(), (std::size_t)n) != SSH_OK)
        {
            ssh_scp_close(scp); ssh_scp_free(scp);
            return false;
        }
    }

    ssh_scp_close(scp);
    ssh_scp_free(scp);
#pragma GCC diagnostic pop
    return true;
}

bool SSHJournalReader::ExecuteCommand(const std::string& command,
                                       std::string&       output)
{
    if (!m_session) return false;

    ssh_channel ch = ssh_channel_new(m_session);
    if (!ch) return false;

    if (ssh_channel_open_session(ch) != SSH_OK ||
        ssh_channel_request_exec(ch, command.c_str()) != SSH_OK)
    {
        ssh_channel_free(ch);
        return false;
    }

    std::array<char, 8192> buf;
    output.clear();

    while (!ssh_channel_is_eof(ch))
    {
        int n = ssh_channel_read_timeout(ch, buf.data(), buf.size(), 0, 300);
        if (n > 0) output.append(buf.data(), (std::size_t)n);
        else if (n < 0) break;

        int e = ssh_channel_read_timeout(ch, buf.data(), buf.size(), 1, 0);
        if (e > 0) output.append(buf.data(), (std::size_t)e);
        else if (e < 0) break;
    }

    ssh_channel_send_eof(ch);
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    return true;
}

void SSHJournalReader::ReadLoop()
{
    std::array<char, 8192> buf;
    std::string stdPending, errPending;

    while (m_running)
    {
        int n = ssh_channel_read_timeout(m_channel, buf.data(), buf.size(), 0, 200);
        if (n > 0)  DispatchBuffer(stdPending, buf.data(), (std::size_t)n);
        else if (n < 0) break;

        int e = ssh_channel_read_timeout(m_channel, buf.data(), buf.size(), 1, 0);
        if (e > 0)  DispatchBuffer(errPending, buf.data(), (std::size_t)e);
        else if (e < 0) break;

        if (ssh_channel_is_eof(m_channel)) break;
    }

    if (!stdPending.empty()) DispatchLine(std::move(stdPending));
    if (!errPending.empty()) DispatchLine(std::move(errPending));

    m_running = false;
}

void SSHJournalReader::DispatchBuffer(std::string& pending,
                                       const char*  data,
                                       std::size_t  size)
{
    pending.append(data, size);
    std::size_t start = 0;

    for (;;)
    {
        std::size_t nl = pending.find('\n', start);
        if (nl == std::string::npos) break;
        DispatchLine(pending.substr(start, nl - start));
        start = nl + 1;
    }

    pending.erase(0, start);
}

void SSHJournalReader::DispatchLine(std::string line)
{
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return;

    LogCallback cb;
    {
        std::lock_guard<std::mutex> lk(m_callbackMutex);
        cb = m_callback;
    }
    if (cb) cb(line);
}
