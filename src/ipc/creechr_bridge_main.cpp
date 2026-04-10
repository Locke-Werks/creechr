// creechr-bridge.exe
//
// the bridge between the chrome / edge browser extension and the
// always-on creechr.exe pet. chrome spawns this binary every time
// the extension's background script calls connectNative('com.creechr.bridge'),
// hands us its stdin/stdout, and expects us to speak the native messaging
// protocol: 4-byte little-endian length prefix followed by a UTF-8 json
// message body. for either direction.
//
// CURRENT STATE (v1.0 plumbing): this binary just logs every incoming
// message to %LOCALAPPDATA%\creechr\creechr\bridge.log and replies with
// a simple ack so the extension's port stays open. the actual forwarding
// to a running creechr.exe (over local named pipe per spec §7.1) is
// not yet implemented — it's the next thing to land. when it is,
// drop a CreechrPipeClient into this same loop.
//
// the protocol max message size per the chrome docs is 1MB. anything
// bigger and we just bail with an error reply. the messages we actually
// expect (scan_targets / steal / restore) are all < 100 KB.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#  include <stdlib.h>
#  include <windows.h>
#endif

namespace {

constexpr uint32_t kMaxMessageBytes = 1024u * 1024u; // 1 MB, per chrome docs

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

void appendLog(const std::string& line)
{
    std::ofstream f(logPath(), std::ios::app);
    if (!f) return;
    f << line << '\n';
}

bool readExactly(void* buf, size_t n)
{
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < n) {
        size_t want = n - got;
        size_t r = std::fread(p + got, 1, want, stdin);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool writeMessage(const std::string& body)
{
    const uint32_t len = static_cast<uint32_t>(body.size());
    if (std::fwrite(&len, 4, 1, stdout) != 1) return false;
    if (len > 0 && std::fwrite(body.data(), 1, len, stdout) != len) return false;
    std::fflush(stdout);
    return true;
}

} // namespace

int main()
{
#ifdef _WIN32
    // chrome native messaging requires raw binary on stdin/stdout. by
    // default the C runtime translates LF/CRLF on text-mode handles —
    // that's fine for plain ascii but it WILL eat 0x0D bytes inside the
    // 4-byte length prefix and corrupt every message. fix it.
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    appendLog("creechr-bridge: started");

    while (true) {
        uint32_t len = 0;
        if (!readExactly(&len, 4)) {
            appendLog("creechr-bridge: stdin closed, exiting");
            return 0;
        }
        if (len == 0 || len > kMaxMessageBytes) {
            appendLog("creechr-bridge: invalid message length " + std::to_string(len));
            return 1;
        }
        std::vector<char> buf(len);
        if (!readExactly(buf.data(), len)) {
            appendLog("creechr-bridge: short read on body, exiting");
            return 1;
        }
        std::string body(buf.data(), len);
        appendLog("creechr-bridge: rx " + body);

        // ack so the extension knows we're alive. real forwarding to
        // creechr.exe over a local named pipe lands later.
        const std::string reply =
            R"({"type":"ack","note":"bridge alive, forwarding not yet wired"})";
        if (!writeMessage(reply)) {
            appendLog("creechr-bridge: write failed, exiting");
            return 1;
        }
    }
}
