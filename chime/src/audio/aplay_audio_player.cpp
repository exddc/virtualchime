#include "chime/audio_player.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "vc/logging/logger.h"
#include "vc/util/filesystem.h"
#include "vc/util/platform.h"
#include "vc/util/strings.h"

namespace chime {

AplayAudioPlayer::AplayAudioPlayer(vc::logging::Logger& logger) : logger_(logger) {}

void AplayAudioPlayer::Play(const std::string& path, int volume_percent) {
  bool expected = false;
  if (!playing_.compare_exchange_strong(expected, true)) {
    logger_.Warn("audio", "already playing, skipping new request");
    return;
  }

  if (!vc::util::IsLinux()) {
    logger_.Info("audio", "(local) would play '" + path + "' volume=" +
                              std::to_string(volume_percent) + "%");
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
    logger_.Info("audio", "playing '" + path + "' at " +
                              std::to_string(effective_volume) + "%");

    const std::string set_volume_cmd =
        "amixer -q sset PCM \"" + std::to_string(effective_volume) +
        "%\" >/dev/null 2>&1";
    const int volume_rc = std::system(set_volume_cmd.c_str());
    if (volume_rc != 0) {
      logger_.Warn("audio", "failed to set volume via amixer, code=" +
                                std::to_string(volume_rc));
    }

    const std::string cmd =
        "aplay -q \"" + vc::util::EscapeShellDoubleQuotes(path) +
        "\" 2>/dev/null";
    const int rc = std::system(cmd.c_str());

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    if (rc != 0) {
      logger_.Error("audio", "aplay failed with code " + std::to_string(rc));
    } else {
      logger_.Info("audio",
                   "playback complete in " + std::to_string(elapsed_ms) + "ms");
    }

    playing_ = false;
  }).detach();
}

bool AplayAudioPlayer::IsPlaying() const { return playing_.load(); }

}  // namespace chime
