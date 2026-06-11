#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// Cross-platform TTY/serial reader.
// On Linux: opens /dev/ttyS*, /dev/ttyUSB*, /dev/ttyACM* directly via termios.
// On Windows: opens COM ports via Win32 CreateFile.
class SerialReader
{
public:
    using LineCallback = std::function<void(const std::string&)>;

    // Available baud rates offered in the UI
    static std::vector<int> CommonBaudRates();

    // Enumerate likely serial ports on the current OS
    static std::vector<std::string> ListPorts();

    SerialReader();
    ~SerialReader();

    SerialReader(const SerialReader&)            = delete;
    SerialReader& operator=(const SerialReader&) = delete;

    // port  = e.g. "/dev/ttyUSB0" or "COM3"
    // baud  = e.g. 115200
    // Returns false on open/config failure; call LastError() for details.
    bool Open(const std::string& port, int baud);
    void Close();
    bool IsOpen() const;

    void SetCallback(LineCallback cb);
    std::string LastError() const { return m_lastError; }

private:
    void ReadLoop();
    void DispatchBuffer(std::string& pending, const char* data, std::size_t n);
    void DispatchLine(std::string line);

#ifdef _WIN32
    void* m_handle{nullptr};   // HANDLE
#else
    int   m_fd{-1};
#endif

    std::atomic_bool m_running{false};
    std::thread      m_thread;
    std::mutex       m_cbMutex;
    LineCallback     m_callback;
    std::string      m_lastError;
};
