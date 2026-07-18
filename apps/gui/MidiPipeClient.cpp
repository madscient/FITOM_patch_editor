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
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kCC = 0xB0;
constexpr uint8_t kProgramChange = 0xC0;
constexpr uint8_t kSysExStart = 0xF0;
constexpr uint8_t kSysExEnd = 0xF7;
// FITOM_X's private SysEx prefix (docs/plugin-midi-pipe.md section 5.2):
// F0 00 48 01 <sub-cmd> ... F7 - "00 48" is a 3-byte extended MIDI
// manufacturer ID (00 followed by 2 more bytes), "01" is a fixed byte
// preceding sub-cmd.
constexpr uint8_t kFitomSysExPrefix[] = {0x00, 0x48, 0x01};
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

bool MidiPipeClient::selectDevice(uint8_t channel, uint8_t voicePatchTypeCc0, uint8_t hwBank, uint8_t hwProg) {
    const uint8_t ch = channel & 0x0F;
    std::vector<uint8_t> msg = {
        static_cast<uint8_t>(kCC | ch), 0x00, voicePatchTypeCc0, // CC#0: bank select MSB (direct device select)
        static_cast<uint8_t>(kCC | ch), 0x20, hwBank,            // CC#32: bank select LSB (HwBank index)
        static_cast<uint8_t>(kProgramChange | ch), hwProg,       // program change
    };
    return sendRaw(msg);
}

bool MidiPipeClient::sendParamOverride(uint8_t subCmd, uint8_t channel, const std::string& json) {
    std::vector<uint8_t> msg;
    msg.push_back(kSysExStart);
    msg.insert(msg.end(), std::begin(kFitomSysExPrefix), std::end(kFitomSysExPrefix));
    msg.push_back(subCmd);
    msg.push_back(0x00);            // target-type = 0x00 (channel)
    msg.push_back(channel & 0x0F);  // target-addr for channel target: 1 byte, MIDI channel
    msg.push_back(0x00);            // layer = 0 (single-layer preview)
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(kSysExEnd);
    return sendRaw(msg);
}

bool MidiPipeClient::sendHwPatchOverride(uint8_t channel, const std::string& json) {
    return sendParamOverride(0x01, channel, json);
}
bool MidiPipeClient::sendSwPatchOverride(uint8_t channel, const std::string& json) {
    return sendParamOverride(0x02, channel, json);
}

bool MidiPipeClient::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return sendRaw({static_cast<uint8_t>(kNoteOn | (channel & 0x0F)), note, velocity});
}
bool MidiPipeClient::noteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return sendRaw({static_cast<uint8_t>(kNoteOff | (channel & 0x0F)), note, velocity});
}
bool MidiPipeClient::sendControlChange(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    return sendRaw({static_cast<uint8_t>(kCC | (channel & 0x0F)), ccNumber, value});
}
bool MidiPipeClient::allSoundOff(uint8_t channel) {
    return sendRaw({static_cast<uint8_t>(kCC | (channel & 0x0F)), 0x78, 0x00});
}
bool MidiPipeClient::resetAllControllers(uint8_t channel) {
    return sendRaw({static_cast<uint8_t>(kCC | (channel & 0x0F)), 0x79, 0x00});
}
