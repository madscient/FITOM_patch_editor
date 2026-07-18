#include "MidiPipeClient.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

// FITOM_X's channel-assignment handshake (docs/plugin-midi-pipe.md 4.1):
// F0 00 48 01 03 <ch> F7, written once immediately after it accepts a
// connection and before anything else.
constexpr size_t kHandshakeSize = 7;

bool isValidHandshake(const uint8_t* buf) {
    return buf[0] == 0xF0 && buf[1] == 0x00 && buf[2] == 0x48 && buf[3] == 0x01 && buf[4] == 0x03 && buf[6] == 0xF7;
}

#ifdef _WIN32
bool readExact(HANDLE h, uint8_t* buf, DWORD count) {
    DWORD total = 0;
    while (total < count) {
        DWORD got = 0;
        if (!ReadFile(h, buf + total, count - total, &got, nullptr) || got == 0) return false;
        total += got;
    }
    return true;
}
#else
bool readExact(int fd, uint8_t* buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        const ssize_t got = recv(fd, buf + total, count - total, 0);
        if (got <= 0) return false;
        total += static_cast<size_t>(got);
    }
    return true;
}
#endif

} // namespace

MidiPipeClient::~MidiPipeClient() { disconnect(); }

bool MidiPipeClient::isConnected() const {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

void MidiPipeClient::disconnect() {
#ifdef _WIN32
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

bool MidiPipeClient::ensureConnected() {
    if (isConnected()) return true;
    rejectedForCapacity_ = false;
#ifdef _WIN32
    // GENERIC_READ is required (not just GENERIC_WRITE) to receive the
    // channel-assignment handshake below.
    HANDLE h = CreateFileA("\\\\.\\pipe\\FITOM_X_MIDI", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) return false; // FITOM_X isn't running - normal offline condition
    uint8_t handshake[kHandshakeSize];
    if (!readExact(h, handshake, static_cast<DWORD>(kHandshakeSize)) || !isValidHandshake(handshake)) {
        // Connected at the OS level but FITOM_X sent nothing before
        // hanging up - it's already serving 16 other connections (its
        // documented hard cap) and refused this one.
        CloseHandle(h);
        rejectedForCapacity_ = true;
        return false;
    }
    assignedChannel_ = handshake[5];
    handle_ = h;
    return true;
#else
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/fitom_x_midi.sock", sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return false; // FITOM_X isn't running - normal offline condition
    }
    uint8_t handshake[kHandshakeSize];
    if (!readExact(fd, handshake, kHandshakeSize) || !isValidHandshake(handshake)) {
        close(fd);
        rejectedForCapacity_ = true;
        return false;
    }
    assignedChannel_ = handshake[5];
    fd_ = fd;
    return true;
#endif
}

bool MidiPipeClient::sendRaw(const std::vector<uint8_t>& bytes) {
    if (!ensureConnected()) return false;
#ifdef _WIN32
    DWORD written = 0;
    BOOL ok =
        WriteFile(static_cast<HANDLE>(handle_), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    if (!ok || written != bytes.size()) {
        disconnect();
        return false;
    }
    return true;
#else
    ssize_t written = write(fd_, bytes.data(), bytes.size());
    if (written < 0 || static_cast<size_t>(written) != bytes.size()) {
        disconnect();
        return false;
    }
    return true;
#endif
}
