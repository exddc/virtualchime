#include "chime/audio_player.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <cstdio>
#include <unistd.h>

#include "vc/logging/logger.h"
#include "vc/util/filesystem.h"
#include "vc/util/platform.h"
#include "vc/util/strings.h"

namespace chime {
namespace {

constexpr std::array<const char *, 9> kMixerControlCandidates = {
    "PCM",
    "Speaker",
    "Master",
    "Digital",
    "Playback",
    "DAC",
    "Headphone",
    "PCM Playback Volume",
    "Digital Playback Volume",
};

struct MixerSetResult {
    bool success = false;
    std::string control_name;
};

uint16_t ReadLe16(const std::vector<uint8_t> &data, std::size_t offset) {
    return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t ReadLe32(const std::vector<uint8_t> &data, std::size_t offset) {
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void WriteLe16(std::vector<uint8_t> *data, std::size_t offset, uint16_t value) {
    (*data)[offset] = static_cast<uint8_t>(value & 0xFF);
    (*data)[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

bool ReadFileBytes(const std::string &path, std::vector<uint8_t> *output) {
    if (output == nullptr) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    output->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return file.good() || file.eof();
}

bool WriteAllToFileDescriptor(int fd, const std::vector<uint8_t> &data) {
    const uint8_t *buffer = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = write(fd, buffer, static_cast<size_t>(remaining));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        buffer += static_cast<std::size_t>(written);
        remaining -= static_cast<std::size_t>(written);
    }
    return fsync(fd) == 0;
}

bool ScalePcmWavInMemory(std::vector<uint8_t> *wav_bytes, int effective_volume, std::string *error) {
    if (wav_bytes == nullptr) {
        if (error != nullptr) {
            *error = "internal error";
        }
        return false;
    }
    if (wav_bytes->size() < 44) {
        if (error != nullptr) {
            *error = "wav too small";
        }
        return false;
    }

    const std::string riff(reinterpret_cast<const char *>(wav_bytes->data()), 4);
    const std::string wave(reinterpret_cast<const char *>(wav_bytes->data() + 8), 4);
    if (riff != "RIFF" || wave != "WAVE") {
        if (error != nullptr) {
            *error = "not a RIFF/WAVE file";
        }
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint16_t bits_per_sample = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    std::size_t cursor = 12;
    while (cursor <= wav_bytes->size() - 8) {
        const std::size_t chunk_header = cursor;
        const uint32_t chunk_size = ReadLe32(*wav_bytes, chunk_header + 4);
        const std::size_t chunk_data = chunk_header + 8;
        if (static_cast<std::size_t>(chunk_size) > wav_bytes->size() - chunk_data) {
            if (error != nullptr) {
                *error = "corrupt wav chunk size";
            }
            return false;
        }
        const std::size_t chunk_end = chunk_data + static_cast<std::size_t>(chunk_size);

        const std::string chunk_id(reinterpret_cast<const char *>(wav_bytes->data() + chunk_header), 4);
        if (chunk_id == "fmt ") {
            if (chunk_size < 16) {
                if (error != nullptr) {
                    *error = "invalid fmt chunk";
                }
                return false;
            }
            audio_format = ReadLe16(*wav_bytes, chunk_data + 0);
            bits_per_sample = ReadLe16(*wav_bytes, chunk_data + 14);
            have_fmt = true;
        } else if (chunk_id == "data") {
            data_offset = chunk_data;
            data_size = static_cast<std::size_t>(chunk_size);
            have_data = true;
        }

        const std::size_t padding = (chunk_size % 2 == 1) ? 1 : 0;
        if (padding > wav_bytes->size() - chunk_end) {
            if (error != nullptr) {
                *error = "corrupt wav chunk padding";
            }
            return false;
        }
        cursor = chunk_end + padding;
    }

    if (!have_fmt || !have_data) {
        if (error != nullptr) {
            *error = "missing fmt or data chunk";
        }
        return false;
    }
    if (audio_format != 1) {
        if (error != nullptr) {
            *error = "unsupported wav encoding (only PCM supported)";
        }
        return false;
    }

    const double gain = static_cast<double>(effective_volume) / 100.0;
    if (bits_per_sample == 16) {
        for (std::size_t i = 0; i + 1 < data_size; i += 2) {
            const std::size_t sample_offset = data_offset + i;
            const int16_t sample = static_cast<int16_t>(static_cast<uint16_t>((*wav_bytes)[sample_offset]) |
                                                        (static_cast<uint16_t>((*wav_bytes)[sample_offset + 1]) << 8));
            const int scaled = static_cast<int>(std::lround(static_cast<double>(sample) * gain));
            const int clamped = std::clamp(scaled, -32768, 32767);
            WriteLe16(wav_bytes, sample_offset, static_cast<uint16_t>(static_cast<int16_t>(clamped)));
        }
        return true;
    }

    if (bits_per_sample == 8) {
        for (std::size_t i = 0; i < data_size; ++i) {
            const std::size_t sample_offset = data_offset + i;
            const int centered = static_cast<int>((*wav_bytes)[sample_offset]) - 128;
            const int scaled = static_cast<int>(std::lround(static_cast<double>(centered) * gain));
            const int clamped = std::clamp(scaled, -128, 127);
            (*wav_bytes)[sample_offset] = static_cast<uint8_t>(clamped + 128);
        }
        return true;
    }

    if (error != nullptr) {
        *error = "unsupported bits_per_sample=" + std::to_string(bits_per_sample);
    }
    return false;
}

bool CreateSoftwareScaledWav(const std::string &source_path, int effective_volume, std::string *output_path,
                             std::string *error) {
    if (output_path == nullptr) {
        if (error != nullptr) {
            *error = "internal error";
        }
        return false;
    }

    std::vector<uint8_t> wav_bytes;
    if (!ReadFileBytes(source_path, &wav_bytes)) {
        if (error != nullptr) {
            *error = "failed to read source wav";
        }
        return false;
    }
    if (!ScalePcmWavInMemory(&wav_bytes, effective_volume, error)) {
        return false;
    }

    char temp_path[] = "/tmp/chime-softvol-XXXXXX";
    const int fd = mkstemp(temp_path);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "failed to create temp file";
        }
        return false;
    }
    const bool write_ok = WriteAllToFileDescriptor(fd, wav_bytes);
    const int close_rc = close(fd);
    if (!write_ok || close_rc != 0) {
        std::remove(temp_path);
        if (error != nullptr) {
            *error = "failed to write scaled wav";
        }
        return false;
    }

    *output_path = temp_path;
    return true;
}

MixerSetResult TrySetVolumeWithAmixer(int effective_volume) {
    for (const char *control_name : kMixerControlCandidates) {
        const std::string set_volume_cmd = "amixer -q sset \"" + vc::util::EscapeShellDoubleQuotes(control_name) +
                                           "\" \"" + std::to_string(effective_volume) + "%\" >/dev/null 2>&1";
        if (std::system(set_volume_cmd.c_str()) == 0) {
            return {true, control_name};
        }
    }
    return {};
}

std::string MixerCandidatesForLog() {
    std::string out;
    for (std::size_t i = 0; i < kMixerControlCandidates.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += kMixerControlCandidates[i];
    }
    return out;
}

} // namespace

AplayAudioPlayer::AplayAudioPlayer(vc::logging::Logger &logger) : logger_(logger) {}

void AplayAudioPlayer::Play(const std::string &path, int volume_percent) {
    bool expected = false;
    if (!playing_.compare_exchange_strong(expected, true)) {
        logger_.Warn("audio", "already playing, skipping new request");
        return;
    }

    if (!vc::util::IsLinux()) {
        logger_.Info("audio", "(local) would play '" + path + "' volume=" + std::to_string(volume_percent) + "%");
        playing_ = false;
        return;
    }

    if (!vc::util::FileExists(path)) {
        logger_.Error("audio", "sound file not found: " + path);
        playing_ = false;
        return;
    }

    const int effective_volume = std::clamp(volume_percent, 0, 100);

    std::thread([this, path, effective_volume]() {
        const auto started = std::chrono::steady_clock::now();
        logger_.Info("audio", "playing '" + path + "' at " + std::to_string(effective_volume) + "%");

        const MixerSetResult mixer_result = TrySetVolumeWithAmixer(effective_volume);
        std::string playback_path = path;
        std::string temporary_scaled_path;
        if (!mixer_result.success) {
            logger_.Warn("audio", "failed to set volume via amixer using known controls; ring volume "
                                  "setting may have no audible effect (tried: " +
                                      MixerCandidatesForLog() + ")");

            if (effective_volume < 100) {
                std::string scale_error;
                if (CreateSoftwareScaledWav(path, effective_volume, &temporary_scaled_path, &scale_error)) {
                    playback_path = temporary_scaled_path;
                    logger_.Info("audio",
                                 "using software-scaled wav fallback at " + std::to_string(effective_volume) + "%");
                } else {
                    logger_.Warn("audio", "software volume fallback unavailable: " + scale_error);
                }
            }
        } else {
            logger_.Info("audio", "applied mixer control '" + mixer_result.control_name + "' to " +
                                      std::to_string(effective_volume) + "%");
        }

        const std::string cmd = "aplay -q \"" + vc::util::EscapeShellDoubleQuotes(playback_path) + "\" 2>/dev/null";
        const int rc = std::system(cmd.c_str());

        if (!temporary_scaled_path.empty()) {
            std::remove(temporary_scaled_path.c_str());
        }

        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if (rc != 0) {
            logger_.Error("audio", "aplay failed with code " + std::to_string(rc));
        } else {
            logger_.Info("audio", "playback complete in " + std::to_string(elapsed_ms) + "ms");
        }

        playing_ = false;
    }).detach();
}

bool AplayAudioPlayer::IsPlaying() const {
    return playing_.load();
}

} // namespace chime
