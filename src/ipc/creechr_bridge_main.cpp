// creechr-bridge.exe
//
// the bridge between the chrome / edge browser extension and the
// always-on creechr.exe pet. chrome spawns this binary when the
// extension's background script calls connectNative('com.creechr.bridge'),
// hands us its stdin/stdout, and expects us to speak the native
// messaging protocol on those handles: 4-byte little-endian length
// prefix followed by a UTF-8 json message body, in either direction.
//
// on the OTHER side we open a named pipe to the always-on creechr.exe
// (\\.\pipe\creechr-extension, served by ExtensionPipeServer over
// there) and forward messages back and forth. on the pipe side the
// protocol is newline-delimited json — simpler, no length prefix.
//
// translation table:
//
//   chrome -> us (length-prefix)         we forward as is to creechr
//   creechr -> us (newline-delim)        we forward as length-prefix to chrome
//
// we run two threads: the main thread reads chrome's stdin and writes
// the pipe, plus a worker thread reads the pipe and writes chrome's
// stdout. exiting either side closes the other.
//
// pure stdlib, no Qt. small + fast to spawn (chrome restarts us per
// connectNative call).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#  include <stdlib.h>
#  include <windows.h>
#endif

namespace {

constexpr uint32_t kMaxMessageBytes = 1024u * 1024u;
constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\creechr-extension";

std::string logPath()
{
#ifdef _WIN32
    char* localAppData = nullptr;
    size_t len = 0;
    if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") != 0 || !localAppData) {
        return "creechr-bridge.log";
    }
    std::string out = std::string(localAppData) + "\\creechr\\creechr\\bridge.log";
    free(localAppData);
    return out;
#else
    return "/tmp/creechr-bridge.log";
#endif
}

std::mutex g_logMutex;
void appendLog(const std::string& line)
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    std::ofstream f(logPath(), std::ios::app);
    if (!f) return;
    f << line << '\n';
}

// --- chrome stdio side (length-prefix protocol) ---

bool readExactlyStdin(void* buf, size_t n)
{
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < n) {
        const size_t want = n - got;
        const size_t r = std::fread(p + got, 1, want, stdin);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

std::mutex g_stdoutMutex;
bool writeStdoutMessage(const std::string& body)
{
    std::lock_guard<std::mutex> lk(g_stdoutMutex);
    const uint32_t len = static_cast<uint32_t>(body.size());
    if (std::fwrite(&len, 4, 1, stdout) != 1) return false;
    if (len > 0 && std::fwrite(body.data(), 1, len, stdout) != len) return false;
    std::fflush(stdout);
    return true;
}

// --- creechr pipe side (newline-delimited protocol) ---

#ifdef _WIN32
HANDLE g_pipe = INVALID_HANDLE_VALUE;
std::mutex g_pipeMutex;

bool openPipe()
{
    // a few retries with backoff in case creechr.exe hasnt finished
    // creating the server (race on the user opting in via the popup
    // before creechr is ready).
    for (int i = 0; i < 10; ++i) {
        HANDLE h = CreateFileW(kPipeName,
                               GENERIC_READ | GENERIC_WRITE,
                               0,                  // no sharing
                               nullptr,            // default security
                               OPEN_EXISTING,
                               0,                  // synchronous io
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_pipe = h;
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_BUSY) {
            appendLog("bridge: pipe open failed err=" + std::to_string(err));
            return false;
        }
        Sleep(150);
    }
    return false;
}

bool writePipeLine(const std::string& body)
{
    if (g_pipe == INVALID_HANDLE_VALUE) return false;
    std::lock_guard<std::mutex> lk(g_pipeMutex);
    std::string out = body;
    out.push_back('\n');
    DWORD wrote = 0;
    if (!WriteFile(g_pipe, out.data(), static_cast<DWORD>(out.size()), &wrote, nullptr)) {
        return false;
    }
    return wrote == out.size();
}

// reads from the pipe forever in a worker thread, splitting on '\n',
// forwarding each line to chrome's stdout as a length-prefix message.
void pipeReaderLoop(std::atomic<bool>* shouldExit)
{
    std::string buf;
    char chunk[4096];
    while (!shouldExit->load()) {
        DWORD got = 0;
        if (!ReadFile(g_pipe, chunk, sizeof(chunk), &got, nullptr) || got == 0) {
            appendLog("bridge: pipe read returned 0, exiting reader");
            shouldExit->store(true);
            break;
        }
        buf.append(chunk, got);
        while (true) {
            const auto nl = buf.find('\n');
            if (nl == std::string::npos) break;
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (line.empty()) continue;
            appendLog("bridge: pipe->chrome " + line);
            if (!writeStdoutMessage(line)) {
                appendLog("bridge: stdout write failed, exiting reader");
                shouldExit->store(true);
                return;
            }
        }
    }
}
#endif // _WIN32

} // namespace

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    appendLog("creechr-bridge: started");

#ifdef _WIN32
    // try to connect to creechr's pipe. if it isn't running we still
    // need to keep chrome's port alive (otherwise the extension errors)
    // — so on failure we stay in stdio-only mode and just log.
    const bool pipeOk = openPipe();
    appendLog(pipeOk ? "bridge: pipe connected"
                     : "bridge: pipe NOT connected, stdio-only mode");

    std::atomic<bool> shouldExit{false};
    std::thread pipeReader;
    if (pipeOk) {
        // tell creechr who we are. v1.0 doesnt know which extension id
        // we represent — that'd come from chrome's command line args
        // (chrome passes them as ext-id and parent-window). leaving the
        // ext field empty for now is fine.
        std::string hello = R"({"type":"hello","ext":""})";
        writePipeLine(hello);
        pipeReader = std::thread(pipeReaderLoop, &shouldExit);
    }

    // main thread: chrome stdin -> pipe forwarder
    while (!shouldExit.load()) {
        uint32_t len = 0;
        if (!readExactlyStdin(&len, 4)) {
            appendLog("bridge: stdin closed, exiting");
            break;
        }
        if (len == 0 || len > kMaxMessageBytes) {
            appendLog("bridge: invalid len " + std::to_string(len));
            break;
        }
        std::vector<char> buf(len);
        if (!readExactlyStdin(buf.data(), len)) {
            appendLog("bridge: short body read, exiting");
            break;
        }
        std::string body(buf.data(), len);
        appendLog("bridge: chrome->pipe " + body);
        if (pipeOk) {
            if (!writePipeLine(body)) {
                appendLog("bridge: pipe write failed, exiting");
                break;
            }
        } else {
            // no pipe, ack so the extension's port doesnt give up
            const std::string reply =
                R"({"type":"ack","note":"bridge alive, creechr.exe not running"})";
            writeStdoutMessage(reply);
        }
    }

    shouldExit.store(true);
    if (g_pipe != INVALID_HANDLE_VALUE) {
        // unblock the pipe reader's blocking ReadFile by closing the handle
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    if (pipeReader.joinable()) pipeReader.join();
#else
    // non-windows: just drain stdin and ack. the extension is windows-
    // only in practice but the build still has to compile elsewhere.
    while (true) {
        uint32_t len = 0;
        if (!readExactlyStdin(&len, 4)) return 0;
        if (len == 0 || len > kMaxMessageBytes) return 1;
        std::vector<char> buf(len);
        if (!readExactlyStdin(buf.data(), len)) return 1;
        const std::string reply = R"({"type":"ack"})";
        writeStdoutMessage(reply);
    }
#endif
    appendLog("creechr-bridge: exited");
    return 0;
}
