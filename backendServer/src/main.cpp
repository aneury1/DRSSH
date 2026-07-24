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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
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
// Query string helpers
// ---------------------------------------------------------------------------

static std::string urlDecode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int value = 0;
            std::sscanf(s.substr(i + 1, 2).c_str(), "%x", &value);
            out += static_cast<char>(value);
            i += 2;
        }
        else if (s[i] == '+')
        {
            out += ' ';
        }
        else
        {
            out += s[i];
        }
    }

    return out;
}

static std::unordered_map<std::string, std::string> parseQuery(const std::string &query)
{
    std::unordered_map<std::string, std::string> params;

    std::istringstream ss(query);
    std::string pair;

    while (std::getline(ss, pair, '&'))
    {
        auto eq = pair.find('=');

        if (eq == std::string::npos)
            continue;

        params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }

    return params;
}

// Accepts either a unix epoch in seconds ("1721800000") or
// "YYYY-MM-DD HH:MM:SS" (interpreted as UTC). Returns 0 on failure.
static uint64_t parseTimestamp(const std::string &input)
{
    if (!input.empty() && std::all_of(input.begin(), input.end(),
                                       [](unsigned char c) { return std::isdigit(c); }))
    {
        return static_cast<uint64_t>(std::atoll(input.c_str())) * 1000000ULL;
    }

    struct tm tmv{};

    if (strptime(input.c_str(), "%Y-%m-%d %H:%M:%S", &tmv) == nullptr)
        return 0;

    time_t seconds = timegm(&tmv); // UTC; swap for mktime() for local time

    if (seconds < 0)
        return 0;

    return static_cast<uint64_t>(seconds) * 1000000ULL;
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

<form id="filterForm" class="row g-2 mb-3">
  <div class="col-sm-3">
    <label class="form-label">From</label>
    <input type="text" class="form-control" id="fFrom" placeholder="2026-07-24 09:00:00 or epoch">
  </div>
  <div class="col-sm-2">
    <label class="form-label">Service</label>
    <input type="text" class="form-control" id="fService" placeholder="nginx.service">
  </div>
  <div class="col-sm-3">
    <label class="form-label">Contains</label>
    <input type="text" class="form-control" id="fContains" placeholder="substring">
  </div>
  <div class="col-sm-3">
    <label class="form-label">Regex</label>
    <input type="text" class="form-control" id="fRegex" placeholder="timeout|failed">
  </div>
  <div class="col-sm-1 d-flex align-items-end">
    <button type="submit" class="btn btn-primary w-100">Apply</button>
  </div>
  <div class="col-sm-1 d-flex align-items-end">
    <button type="button" id="exportBtn" class="btn btn-outline-secondary w-100">Export</button>
  </div>
</form>

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
const form = document.getElementById('filterForm');
const exportBtn = document.getElementById('exportBtn');

let source = null;

function rowFor(log) {
    const tr = document.createElement('tr');
    tr.dataset.time = log.time;
    tr.dataset.service = log.service;
    tr.dataset.priority = log.priority;
    tr.dataset.message = log.message;
    tr.innerHTML =
        '<td>' + log.time + '</td>' +
        '<td>' + log.service + '</td>' +
        '<td>' + log.priority + '</td>' +
        '<td>' + log.message + '</td>';
    return tr;
}

function exportLogs(event) {
    if (event) {
        event.preventDefault();
        event.stopPropagation();
    }

    const rows = Array.from(tbody.querySelectorAll('tr'));

    if (rows.length === 0) {
        alert('No log rows to export yet.');
        return;
    }

    const lines = rows.map(tr =>
        '[' + tr.dataset.time + '] ' +
        '(' + tr.dataset.service + ') ' +
        tr.dataset.priority.toUpperCase() + ': ' +
        tr.dataset.message
    );

    const blob = new Blob([lines.join('\n') + '\n'], {type: 'text/plain'});
    const url = URL.createObjectURL(blob);

    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const a = document.createElement('a');
    a.href = url;
    a.download = 'journal-logs-' + stamp + '.txt';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    URL.revokeObjectURL(url);
}

exportBtn.type = 'button';
exportBtn.addEventListener('click', exportLogs);

async function loadInitial() {
    const response = await fetch('/logs');
    const logs = await response.json();
    tbody.innerHTML = '';
    logs.forEach(log => tbody.appendChild(rowFor(log)));
}

function currentFilterParams() {
    const params = new URLSearchParams();

    const from = document.getElementById('fFrom').value.trim();
    const service = document.getElementById('fService').value.trim();
    const contains = document.getElementById('fContains').value.trim();
    const regex = document.getElementById('fRegex').value.trim();

    if (from) params.set('from', from);
    if (service) params.set('service', service);
    if (contains) params.set('contains', contains);
    if (regex) params.set('regex', regex);

    return params.toString();
}

function subscribe() {
    if (source) {
        source.close();
    }

    status.textContent = 'connecting…';
    status.className = 'badge bg-secondary';

    const query = currentFilterParams();
    const url = query ? ('/stream?' + query) : '/stream';

    source = new EventSource(url);

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

form.addEventListener('submit', (event) => {
    event.preventDefault();
    subscribe();
});

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
    void streamLogs(int client, const std::string &queryString);
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

    std::string query;
    auto qpos = path.find('?');

    if (qpos != std::string::npos)
    {
        query = path.substr(qpos + 1);
        path = path.substr(0, qpos);
    }

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
        streamLogs(client, query);
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

// GET /stream?from=<epoch|YYYY-MM-DD HH:MM:SS>&service=<name>&contains=<text>&regex=<pattern>
//
//   from     - start point. Omit to start from "now" (live tail only).
//   service  - exact match against _SYSTEMD_UNIT or SYSLOG_IDENTIFIER.
//   contains - substring match against MESSAGE.
//   regex    - ECMAScript regex match against MESSAGE.
// All filters are optional and combine with AND (service itself matches
// unit OR identifier).
void HttpServer::streamLogs(int client, const std::string &queryString)
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

    auto params = parseQuery(queryString);

    std::string service  = params.count("service")  ? params["service"]  : "";
    std::string contains = params.count("contains") ? params["contains"] : "";
    std::string regexStr = params.count("regex")    ? params["regex"]    : "";
    std::string from     = params.count("from")     ? params["from"]     : "";

    std::optional<std::regex> pattern;

    if (!regexStr.empty())
    {
        try
        {
            pattern.emplace(regexStr, std::regex::ECMAScript);
        }
        catch (const std::regex_error &)
        {
            pattern.reset(); // bad pattern from client - just ignore the filter
        }
    }

    sd_journal *journal = nullptr;

    if (sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY) < 0)
    {
        close(client);
        return;
    }

    if (!service.empty())
    {
        std::string matchUnit  = "_SYSTEMD_UNIT=" + service;
        std::string matchIdent = "SYSLOG_IDENTIFIER=" + service;

        sd_journal_add_match(journal, matchUnit.c_str(), 0);
        sd_journal_add_disjunction(journal); // OR the two match groups
        sd_journal_add_match(journal, matchIdent.c_str(), 0);
    }

    if (!from.empty())
    {
        uint64_t usec = parseTimestamp(from);

        if (usec > 0)
            sd_journal_seek_realtime_usec(journal, usec);
        else
            sd_journal_seek_tail(journal); // bad timestamp - fall back to live tail
    }
    else
    {
        // Position at the tail so the next sd_journal_next() call only
        // returns entries that arrive from this point forward.
        sd_journal_seek_tail(journal);
        sd_journal_previous(journal);
    }

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

        if (!contains.empty() && entry.message.find(contains) == std::string::npos)
            continue;

        if (pattern && !std::regex_search(entry.message, *pattern))
            continue;

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
    int port = 8080;

    if (argc > 1)
        port = std::atoi(argv[1]);

    HttpServer server(port);
    server.start();

    return 0;
}