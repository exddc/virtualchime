#include "chime/audio_player.h"

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

void AplayAudioPlayer::Play(const std::string& path) {
  bool expected = false;
  if (!playing_.compare_exchange_strong(expected, true)) {
    logger_.Warn("audio", "already playing, skipping new request");
    return;
  }

  if (!vc::util::IsLinux()) {
    logger_.Info("audio", "(local) would play '" + path + "'");
    playing_ = false;
    return;
  }

  if (!vc::util::FileExists(path)) {
    logger_.Error("audio", "sound file not found: " + path);
    playing_ = false;
    return;
  }

  std::thread([this, path]() {
    const auto started = std::chrono::steady_clock::now();
    logger_.Info("audio", "playing '" + path + "'");

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
