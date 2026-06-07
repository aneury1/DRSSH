#pragma once
#include <libssh/libssh.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <string>
#include <array>

class SSHJournalReader
{
public:
    using LogCallback = std::function<void(const std::string&)>;

    SSHJournalReader();
    ~SSHJournalReader();

    SSHJournalReader(const SSHJournalReader&)            = delete;
    SSHJournalReader& operator=(const SSHJournalReader&) = delete;

    bool Connect(const std::string& host,
                 int                port,
                 const std::string& user,
                 const std::string& password);

    bool Start(const std::string& command = "journalctl -f -o short-iso");
    void Stop();
    bool IsRunning() const;

    void SetCallback(LogCallback cb);

    bool UploadFile(const std::string& localPath,
                    const std::string& remoteDirectory);

    bool ExecuteCommand(const std::string& command,
                        std::string&       output);

private:
    void ReadLoop();
    void DispatchBuffer(std::string& pending,
                        const char*  data,
                        std::size_t  size);
    void DispatchLine(std::string line);

    ssh_session      m_session{nullptr};
    ssh_channel      m_channel{nullptr};
    std::atomic_bool m_running{false};
    std::thread      m_thread;
    std::mutex       m_callbackMutex;
    LogCallback      m_callback;
};
