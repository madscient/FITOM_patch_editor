#include "MidiPipeClient.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
    HANDLE h = CreateFileA("\\\\.\\pipe\\FITOM_X_MIDI", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
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
        return false;
    }
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
