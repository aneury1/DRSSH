// journal_server.cpp
//
// Single-file HTTP server that serves systemd journal entries as JSON
// and streams new entries live via Server-Sent Events (SSE).
//
// Only dependency beyond the standard library: libsystemd (sd-journal.h).
//
// Build:
//   g++ -std=c++17 -O2 -o journal_server journal_server.cpp -lsystemd
//
// Run:
//   ./journal_server 8080
//
// Endpoints:
//   GET /            -> serves www/index.html (static files served from ./www)
//   GET /logs        -> last 100 journal entries as a JSON array
//   GET /stream      -> text/event-stream, pushes new journal entries as they arrive
//   GET /viewer      -> built-in HTML viewer (table + live tail via EventSource)

#include <systemd/sd-journal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Journal entry model
// ---------------------------------------------------------------------------

struct LogEntry
{
    std::string time;
    std::string service;
    std::string priority;
    std::string message;
};

static std::string getField(sd_journal *journal, const char *field)
{
    const void *data = nullptr;
    size_t length = 0;

    if (sd_journal_get_data(journal, field, &data, &length) < 0)
        return "";

    std::string raw(static_cast<const char *>(data), length);

    auto eq = raw.find('=');

    if (eq == std::string::npos)
        return "";

    return raw.substr(eq + 1);
}

static std::string getTimestamp(sd_journal *journal)
{
    uint64_t usec = 0;

    if (sd_journal_get_realtime_usec(journal, &usec) < 0)
        return "";

    time_t seconds = static_cast<time_t>(usec / 1000000);

    char buffer[32]{};
    struct tm tmv{};

    localtime_r(&seconds, &tmv);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmv);

    return std::string(buffer);
}

static std::string getPriority(sd_journal *journal)
{
    std::string raw = getField(journal, "PRIORITY");

    if (raw.empty())
        return "info";

    static const char *names[] = {
        "emerg", "alert", "crit", "err",
        "warning", "notice", "info", "debug"};

    int value = std::atoi(raw.c_str());

    if (value >= 0 && value < 8)
        return names[value];

    return raw;
}

static std::string getService(sd_journal *journal)
{
    std::string service = getField(journal, "_SYSTEMD_UNIT");

    if (service.empty())
        service = getField(journal, "SYSLOG_IDENTIFIER");

    if (service.empty())
        service = getField(journal, "_COMM");

    return service;
}

static LogEntry extractEntry(sd_journal *journal)
{
    LogEntry entry;

    entry.time = getTimestamp(journal);
    entry.service = getService(journal);
    entry.priority = getPriority(journal);
    entry.message = getField(journal, "MESSAGE");

    return entry;
}

// Reads the last `count` entries from the local journal, oldest first.
static std::vector<LogEntry> readLastEntries(int count)
{
    std::vector<LogEntry> entries;

    sd_journal *journal = nullptr;

    if (sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY) < 0)
        return entries;

    sd_journal_seek_tail(journal);

    int fetched = 0;

    while (fetched < count && sd_journal_previous(journal) > 0)
    {
        entries.push_back(extractEntry(journal));
        fetched++;
    }

    sd_journal_close(journal);

    std::reverse(entries.begin(), entries.end());

    return entries;
}

// ---------------------------------------------------------------------------
// JSON (hand-rolled, no third-party lib)
// ---------------------------------------------------------------------------

static std::string jsonEscape(const std::string &input)
{
    std::string out;
    out.reserve(input.size());

    for (unsigned char c : input)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
        }
    }

    return out;
}

static std::string toJsonSingle(const LogEntry &e)
{
    std::ostringstream ss;

    ss << "{"
       << "\"time\":\"" << jsonEscape(e.time) << "\","
       << "\"service\":\"" << jsonEscape(e.service) << "\","
       << "\"priority\":\"" << jsonEscape(e.priority) << "\","
       << "\"message\":\"" << jsonEscape(e.message) << "\""
       << "}";

    return ss.str();
}

static std::string toJson(const std::vector<LogEntry> &entries)
{
    std::ostringstream ss;

    ss << "[";

    for (size_t i = 0; i < entries.size(); ++i)
    {
        ss << toJsonSingle(entries[i]);

        if (i + 1 < entries.size())
            ss << ",";
    }

    ss << "]";

    return ss.str();
}

// ---------------------------------------------------------------------------
// HTTP plumbing
// ---------------------------------------------------------------------------

static std::string getMimeType(const std::string &path)
{
    auto dot = path.find_last_of('.');

    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(dot + 1);

    static const std::unordered_map<std::string, std::string> types = {
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"txt", "text/plain"},
    };

    auto it = types.find(ext);

    if (it != types.end())
        return it->second;

    return "application/octet-stream";
}

struct HttpResponse
{
    int status = 200;
    std::string statusText = "OK";
    std::string contentType = "text/plain";
    std::string body;

    std::string toString() const
    {
        std::ostringstream ss;

        ss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "\r\n"
           << body;

        return ss.str();
    }
};

static const char *VIEWER_HTML = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.7/dist/css/bootstrap.min.css" rel="stylesheet">
<title>Systemd Logs</title>
</head>
<body>
<div class="container mt-4">
<h2>Systemd Logs <span id="status" class="badge bg-secondary">connecting…</span></h2>
<table class="table table-striped table-hover">
<thead>
<tr><th>Time</th><th>Service</th><th>Priority</th><th>Message</th></tr>
</thead>
<tbody id="logs"></tbody>
</table>
</div>
<script>
const tbody = document.getElementById('logs');
const status = document.getElementById('status');

function rowFor(log) {
    const tr = document.createElement('tr');
    tr.innerHTML =
        '<td>' + log.time + '</td>' +
        '<td>' + log.service + '</td>' +
        '<td>' + log.priority + '</td>' +
        '<td>' + log.message + '</td>';
    return tr;
}

async function loadInitial() {
    const response = await fetch('/logs');
    const logs = await response.json();
    tbody.innerHTML = '';
    logs.forEach(log => tbody.appendChild(rowFor(log)));
}

function subscribe() {
    const source = new EventSource('/stream');

    source.onopen = () => {
        status.textContent = 'live';
        status.className = 'badge bg-success';
    };

    source.onerror = () => {
        status.textContent = 'disconnected';
        status.className = 'badge bg-danger';
    };

    source.onmessage = (event) => {
        const log = JSON.parse(event.data);
        tbody.appendChild(rowFor(log));
        tbody.scrollIntoView({block: 'end'});
    };
}

loadInitial().then(subscribe);
</script>
</body>
</html>
)HTML";

class HttpServer
{
public:
    explicit HttpServer(int port);
    ~HttpServer();

    void start();

private:
    void acceptLoop();
    void handleClient(int client);
    void streamLogs(int client);
    static std::string readFile(const std::string &path);

    int m_port;
    int m_socket = -1;
};

HttpServer::HttpServer(int port)
    : m_port(port)
{
}

HttpServer::~HttpServer()
{
    if (m_socket > 0)
        close(m_socket);
}

void HttpServer::start()
{
    m_socket = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::perror("bind");
        std::exit(1);
    }

    listen(m_socket, 32);

    std::cout << "Listening on " << m_port << std::endl;

    acceptLoop();
}

void HttpServer::acceptLoop()
{
    while (true)
    {
        int client = accept(m_socket, nullptr, nullptr);

        if (client < 0)
            continue;

        std::thread(&HttpServer::handleClient, this, client).detach();
    }
}

std::string HttpServer::readFile(const std::string &file)
{
    std::ifstream in(file, std::ios::binary);

    if (!in)
        return "";

    std::ostringstream ss;
    ss << in.rdbuf();

    return ss.str();
}

void HttpServer::handleClient(int client)
{
    char buffer[8192]{};

    ssize_t received = recv(client, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0)
    {
        close(client);
        return;
    }

    std::string request(buffer, static_cast<size_t>(received));

    std::string path = "/";

    auto p1 = request.find(' ');
    auto p2 = request.find(' ', p1 + 1);

    if (p1 != std::string::npos && p2 != std::string::npos)
        path = request.substr(p1 + 1, p2 - p1 - 1);

    if (path == "/")
        path = "/index.html";

    if (path == "/logs")
    {
        auto entries = readLastEntries(100);

        HttpResponse response;
        response.contentType = "application/json";
        response.body = toJson(entries);

        auto data = response.toString();
        send(client, data.c_str(), data.size(), 0);

        close(client);
        return;
    }

    if (path == "/stream")
    {
        streamLogs(client);
        return;
    }

    if (path == "/viewer")
    {
        HttpResponse response;
        response.contentType = "text/html";
        response.body = VIEWER_HTML;

        auto data = response.toString();
        send(client, data.c_str(), data.size(), 0);

        close(client);
        return;
    }

    HttpResponse response;
    std::string filename = "www" + path;

    response.body = readFile(filename);

    if (response.body.empty())
    {
        response.status = 404;
        response.statusText = "Not Found";
        response.body = "<h1>404</h1>";
        response.contentType = "text/html";
    }
    else
    {
        response.contentType = getMimeType(filename);
    }

    auto data = response.toString();
    send(client, data.c_str(), data.size(), 0);

    close(client);
}

void HttpServer::streamLogs(int client)
{
    std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    if (send(client, headers.c_str(), headers.size(), 0) < 0)
    {
        close(client);
        return;
    }

    sd_journal *journal = nullptr;

    if (sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY) < 0)
    {
        close(client);
        return;
    }

    // Position at the tail so the next sd_journal_next() call only returns
    // entries that arrive from this point forward.
    sd_journal_seek_tail(journal);
    sd_journal_previous(journal);

    while (true)
    {
        int rc = sd_journal_next(journal);

        if (rc < 0)
            break;

        if (rc == 0)
        {
            // No new entry yet - block until one shows up (or timeout, so we
            // can still notice a dead socket).
            rc = sd_journal_wait(journal, 2000000 /* usec = 2s */);

            if (rc < 0)
                break;

            continue;
        }

        LogEntry entry = extractEntry(journal);

        std::string payload = "data: " + toJsonSingle(entry) + "\n\n";

        if (send(client, payload.c_str(), payload.size(), 0) < 0)
            break;
    }

    sd_journal_close(journal);
    close(client);
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
std::string sport = (argc > 1) ? argv[1] : "9999";
int port = std::stoi(sport);

    if (argc > 1)
        port = std::atoi(argv[1]);

    HttpServer server(port);
    server.start();

    return 0;
}